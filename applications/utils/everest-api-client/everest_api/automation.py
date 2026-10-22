# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Variables, ${...} substitution and the rules behind `on` and `every`.

The commands of the rules run on one worker thread, in the order the rules
fired, so neither the MQTT network threads nor the shell's main thread are
blocked. A rule's conditions are checked on that thread right before its
commands run, so they see what the rules before it did.
"""

import ast
import datetime
import itertools
import json
import math
import operator
import queue
import re
import threading
from dataclasses import dataclass, field
from typing import List, Optional

# the inside of ${name}, ${name.key}, ${name.list[0]}, ${name.a.b[1].c}
_REFERENCE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)((?:\.[A-Za-z0-9_-]+|\[\d+\])*)")
_PATH_PART = re.compile(r"\.([A-Za-z0-9_-]+)|\[(\d+)\]")

# limits that keep an expression from exhausting memory
_MAX_REPEAT = 100000
_MAX_FORMAT_WIDTH = 1000


class SubstitutionError(ValueError):
    pass


def parse_value(text):
    """A JSON value if *text* parses as JSON, the text itself otherwise."""
    try:
        return json.loads(text)
    except ValueError:
        return text


def lookup(value, path):
    """Follow a path like '.a.b[0]' into *value*."""
    for key, index in _PATH_PART.findall(path):
        try:
            value = value[key] if key else value[int(index)]
        except (KeyError, IndexError, TypeError):
            raise SubstitutionError(f"'{path}' not found") from None
    return value


def render(value):
    """Text for a value inside a command line: strings as they are, everything
    else as compact JSON."""
    if isinstance(value, str):
        return value
    if isinstance(value, float) and not math.isfinite(value):
        raise SubstitutionError(f"{value} is not a JSON number")
    return json.dumps(value, separators=(",", ":"))


def _now():
    """The current UTC time as an RFC 3339 timestamp, e.g. 2026-01-01T12:00:00.000Z."""
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def _json(value):
    """*value* as JSON text: a string becomes a quoted and escaped JSON string."""
    return json.dumps(value, separators=(",", ":"))


def _multiply(left, right):
    for sequence, count in ((left, right), (right, left)):
        if isinstance(sequence, (str, list)) and isinstance(count, int) and count > _MAX_REPEAT:
            raise SubstitutionError(f"repeating a value more than {_MAX_REPEAT} times is not allowed")
    return left * right


def _modulo(left, right):
    if isinstance(left, str):
        widths = [int(number) for number in re.findall(r"%[-+ #0]*(\d+)|\.(\d+)", left) for number in number
                  if number]
        if any(width > _MAX_FORMAT_WIDTH for width in widths):
            raise SubstitutionError(f"a format width above {_MAX_FORMAT_WIDTH} is not allowed")
    return left % right


_BINARY = {ast.Add: operator.add, ast.Sub: operator.sub, ast.Mult: _multiply, ast.Div: operator.truediv,
           ast.FloorDiv: operator.floordiv, ast.Mod: _modulo}
_COMPARE = {ast.Eq: operator.eq, ast.NotEq: operator.ne, ast.Lt: operator.lt, ast.LtE: operator.le,
            ast.Gt: operator.gt, ast.GtE: operator.ge, ast.In: lambda a, b: a in b,
            ast.NotIn: lambda a, b: a not in b}
_FUNCTIONS = {"min": min, "max": max, "abs": abs, "round": round, "int": int, "float": float, "now": _now,
              "json": _json}


def evaluate(expression, scope):
    """Evaluate an expression over the variables in *scope*: numbers, strings,
    true/false, lists, dicts, + - * / // %, comparisons including in / not in,
    and/or/not, 'x if c else y', the functions min max abs round int float now
    json, and a.key / a[0] to read into values. Nothing else (no attribute
    calls, no imports) is allowed."""
    def value(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float, str, bool)):
            return node.value
        if isinstance(node, ast.Name):
            if node.id in ("true", "false"):
                return node.id == "true"
            if node.id not in scope:
                raise SubstitutionError(f"unknown variable '{node.id}'")
            return scope[node.id]
        if isinstance(node, (ast.Tuple, ast.List)):
            return [value(element) for element in node.elts]
        if isinstance(node, ast.Dict) and None not in node.keys:
            return {value(key): value(item) for key, item in zip(node.keys, node.values)}
        if isinstance(node, ast.Attribute):
            return lookup(value(node.value), "." + node.attr)
        if isinstance(node, ast.Subscript):
            container, key = value(node.value), value(node.slice)
            try:
                return container[key]
            except (KeyError, IndexError, TypeError):
                raise SubstitutionError(f"[{key!r}] not found") from None
        if isinstance(node, ast.BinOp) and type(node.op) in _BINARY:
            return _BINARY[type(node.op)](value(node.left), value(node.right))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd, ast.Not)):
            operand = value(node.operand)
            return -operand if isinstance(node.op, ast.USub) else +operand if isinstance(node.op, ast.UAdd) \
                else not operand
        if isinstance(node, ast.Compare):
            left = value(node.left)
            for op, comparator in zip(node.ops, node.comparators):
                right = value(comparator)
                if type(op) not in _COMPARE or not _COMPARE[type(op)](left, right):
                    return False
                left = right
            return True
        if isinstance(node, ast.BoolOp):
            values = [value(operand) for operand in node.values]
            return all(values) if isinstance(node.op, ast.And) else any(values)
        if isinstance(node, ast.IfExp):
            return value(node.body) if value(node.test) else value(node.orelse)
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in _FUNCTIONS \
                and not node.keywords:
            return _FUNCTIONS[node.func.id](*[value(arg) for arg in node.args])
        raise SubstitutionError(f"'{ast.unparse(node)}' is not allowed in an expression")

    try:
        tree = ast.parse(expression.strip(), mode="eval")
    except (SyntaxError, ValueError, RecursionError, MemoryError):
        raise SubstitutionError(f"invalid expression '{expression.strip()}'") from None
    try:
        return value(tree.body)
    except SubstitutionError as err:
        raise SubstitutionError(f"'{expression.strip()}': {err}") from None
    except (TypeError, ValueError, ArithmeticError, RecursionError, MemoryError) as err:
        raise SubstitutionError(f"'{expression.strip()}': {type(err).__name__}: {err}") from None


def _references(text):
    """(start, end, inside) of every ${...} in *text*. The inside may contain
    braces and quoted strings, e.g. ${= json({"a": "}"})}."""
    found = []
    position = 0
    while True:
        start = text.find("${", position)
        if start < 0:
            return found
        depth, quote, index = 1, None, start + 2
        while index < len(text) and depth:
            char = text[index]
            if quote:
                if char == "\\":
                    index += 1
                elif char == quote:
                    quote = None
            elif char in "'\"":
                quote = char
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
            index += 1
        if depth:
            return found
        found.append((start, index, text[start + 2:index - 1]))
        position = index


def has_references(text):
    return any(inside.startswith("=") or _REFERENCE.fullmatch(inside) for _, _, inside in _references(text))


def substitute(text, scope):
    """Replace every ${= expression} and ${name.path} in *text* by its value from
    *scope*, in one pass: the values are not searched for references again.
    Anything else written as ${...} is left as it is."""
    parts = []
    position = 0
    for start, end, inside in _references(text):
        if inside.startswith("="):
            replacement = render(evaluate(inside[1:], scope))
        else:
            match = _REFERENCE.fullmatch(inside)
            if not match:
                continue
            name, path = match.groups()
            if name not in scope:
                raise SubstitutionError(f"unknown variable '{name}' in '${{{inside}}}'")
            try:
                replacement = render(lookup(scope[name], path))
            except SubstitutionError as err:
                raise SubstitutionError(f"${{{inside}}}: {err}") from None
        parts.append(text[position:start])
        parts.append(replacement)
        position = end
    parts.append(text[position:])
    return "".join(parts)


def split_commands(text):
    """Split the command of a rule at the ';' outside quotes and ${...}."""
    commands, current, quote, depth = [], [], None, 0
    index = 0
    while index < len(text):
        char = text[index]
        if quote:
            if char == "\\" and index + 1 < len(text):
                current.append(char)
                index += 1
                char = text[index]
            elif char == quote:
                quote = None
        elif char in "'\"":
            quote = char
        elif text.startswith("${", index):
            depth += 1
            current.append(char)
            index += 1
            char = text[index]
        elif char == "{" and depth:
            depth += 1
        elif char == "}" and depth:
            depth -= 1
        elif char == ";" and not depth:
            commands.append("".join(current).strip())
            current = []
            index += 1
            continue
        current.append(char)
        index += 1
    commands.append("".join(current).strip())
    return [command for command in commands if command]


def parse_condition(text):
    """A condition: 'name.path=value', true when the value at the path renders
    as *value*, or '${= expression}', true when the expression is."""
    if text.startswith("${=") and text.endswith("}"):
        return ("expression", text[3:-1])
    path, sep, expected = text.partition("=")
    if not sep or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*((\.[A-Za-z0-9_-]+)|\[\d+\])*", path):
        raise ValueError(f"'{text}' is not a condition: name.path=value or ${{= expression}}")
    return ("path", path, expected)


def describe_condition(condition):
    return f"${{={condition[1]}}}" if condition[0] == "expression" else f"{condition[1]}={condition[2]}"


def condition_holds(condition, scope):
    if condition[0] == "expression":
        return bool(evaluate(condition[1], scope))
    _, path, expected = condition
    name, rest = re.match(r"([A-Za-z_][A-Za-z0-9_]*)(.*)", path).groups()
    if name not in scope:
        return False
    try:
        return render(lookup(scope[name], rest)) == expected
    except SubstitutionError:
        return False


@dataclass
class Rule:
    rule_id: int
    kind: str                          # "on" or "every"
    command: str                       # one or more commands separated by ';'
    trigger: str = ""                  # on: '<module_id>.<op_id>'
    conditions: List[tuple] = field(default_factory=list)
    once: bool = False
    interval: float = 0.0              # every: seconds
    count: Optional[int] = None        # every: remaining runs, None for unlimited
    stop: threading.Event = field(default_factory=threading.Event)
    # no new runs: a --once rule that ran or an every rule out of runs
    done: bool = False

    @property
    def commands(self):
        return split_commands(self.command)

    def describe(self):
        conditions = "".join(f" --if {describe_condition(condition)}" for condition in self.conditions)
        if self.kind == "every":
            times = "" if self.count is None else f" ({self.count} more)"
            return f"every {self.interval:g}s{conditions}{times}: {self.command}"
        return f"on {self.trigger}{conditions}{' --once' if self.once else ''}: {self.command}"


class RuleEngine:
    """Holds the rules and runs their commands with *execute(command, scope)*
    on a worker thread; *report(text)* receives errors. Conditions see the
    variables returned by *variables()* and, for on rules, the message."""

    def __init__(self, execute, report, variables=dict):
        self._execute = execute
        self._report = report
        self._variables = variables
        self._rules = {}
        self._lock = threading.Lock()
        self._ids = itertools.count(1)
        self._jobs = queue.Queue()
        self._worker = threading.Thread(target=self._work, daemon=True)
        self._worker.start()

    def add_on(self, trigger, command, conditions=(), once=False):
        rule = Rule(next(self._ids), "on", command, trigger=trigger, conditions=list(conditions), once=once)
        with self._lock:
            self._rules[rule.rule_id] = rule
        return rule

    def add_every(self, interval, command, count=None, conditions=()):
        rule = Rule(next(self._ids), "every", command, interval=interval, count=count, conditions=list(conditions))
        with self._lock:
            self._rules[rule.rule_id] = rule
        threading.Thread(target=self._tick, args=(rule,), daemon=True).start()
        return rule

    def rules(self):
        with self._lock:
            return list(self._rules.values())

    def remove(self, rule_id=None):
        """Remove one rule, or all of them if rule_id is None; a removed rule
        does not run again, even if it is queued. Returns the removed rules."""
        with self._lock:
            ids = list(self._rules) if rule_id is None else [rule_id] if rule_id in self._rules else []
            removed = [self._rules.pop(i) for i in ids]
        for rule in removed:
            rule.stop.set()
        return removed

    def message_received(self, trigger, scope):
        """Called for every message a client receives; queues the on rules of
        that trigger. Under the lock, so that the rules of one message run
        together and in the order they were created, also when messages arrive
        on several threads."""
        with self._lock:
            for rule in self._rules.values():
                if rule.kind == "on" and rule.trigger == trigger and not rule.done:
                    self._jobs.put((rule, scope))

    def wait_idle(self):
        self._jobs.join()

    def stop(self):
        self.remove()
        self._jobs.put(None)

    def _tick(self, rule):
        while not rule.stop.wait(rule.interval) and not rule.done:
            self._jobs.put((rule, {}))

    def _run(self, rule, scope):
        if rule.stop.is_set() or rule.done:
            return
        try:
            full_scope = {**self._variables(), **scope}
            if not all(condition_holds(condition, full_scope) for condition in rule.conditions):
                return
        except Exception as err:  # noqa: BLE001 - a failing rule must not stop the others
            self._report(f"rule {rule.rule_id} condition: {err}")
            return
        if rule.once:
            rule.done = True
        elif rule.count is not None:
            rule.count -= 1
            rule.done = rule.count <= 0
        if rule.done:
            with self._lock:
                self._rules.pop(rule.rule_id, None)
        for command in rule.commands:
            try:
                self._execute(command, scope)
            except Exception as err:  # noqa: BLE001 - a failing rule must not stop the others
                self._report(f"rule {rule.rule_id} ({command}): {err}")
                return

    def _work(self):
        while True:
            job = self._jobs.get()
            try:
                if job is None:
                    return
                self._run(*job)
            finally:
                self._jobs.task_done()
