// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
// Script of the EVerest API reference pages. The pages work without it; it
// adds expand/collapse all, on-demand schema definitions and copy buttons.

(function () {
  "use strict";

  document.documentElement.classList.add("js");

  // ---- open the <details> around a linked target ----
  function revealHash() {
    var id;
    try {
      id = decodeURIComponent(window.location.hash.slice(1));
    } catch (e) {
      return;
    }
    if (!id) return;
    var target = document.getElementById(id);
    if (!target || !target.closest(".everest-api")) return;
    for (var el = target; el; el = el.parentElement) {
      if (el.tagName === "DETAILS") el.open = true;
    }
    target.scrollIntoView();
  }

  // A schema defined on this page is rendered once in the schemas section;
  // tree nodes referring to it copy that definition in when first opened.
  function inlineDefinition(details) {
    var placeholder = details.querySelector(":scope > .s-inline");
    if (!placeholder) return;
    var definition = document.getElementById(details.getAttribute("data-inline"));
    var source = definition && definition.querySelector(":scope > .s-root");
    if (!source) return;
    var hasUseSiteDescription = details.querySelector(":scope > .s-desc") !== null;
    Array.prototype.forEach.call(source.children, function (child) {
      if (child.classList.contains("s-head")) return;
      if (hasUseSiteDescription && child.classList.contains("s-desc")) return;
      details.insertBefore(child.cloneNode(true), placeholder);
    });
    placeholder.remove();
    details.querySelectorAll("details[data-inline]").forEach(watchInline);
  }

  function watchInline(details) {
    if (details.open) inlineDefinition(details);
    details.addEventListener("toggle", function () {
      if (details.open) inlineDefinition(details);
    });
  }

  // A definition already being shown by an ancestor is a recursive
  // reference; leave it closed so expanding terminates.
  function isRecursive(details) {
    var target = details.getAttribute("data-inline");
    for (var el = details.parentElement; el; el = el.parentElement) {
      if (el.tagName === "DETAILS" && el.getAttribute("data-inline") === target) return true;
    }
    return false;
  }

  function expandAll() {
    document.querySelectorAll(".everest-api details:not([data-inline])").forEach(function (d) { d.open = true; });
    var pending;
    do {
      pending = Array.prototype.filter.call(
        document.querySelectorAll(".everest-api details[data-inline]:not([open])"),
        function (d) { return !isRecursive(d); });
      pending.forEach(function (d) {
        d.open = true;
        inlineDefinition(d);
        d.querySelectorAll("details:not([data-inline])").forEach(function (inner) { inner.open = true; });
      });
    } while (pending.length);
  }

  function addCopyButton(container, getText) {
    if (!navigator.clipboard) return;
    var button = document.createElement("button");
    button.type = "button";
    button.className = "copy";
    button.textContent = "Copy";
    button.addEventListener("click", function (event) {
      event.preventDefault();
      navigator.clipboard.writeText(getText()).then(function () {
        button.textContent = "Copied";
        setTimeout(function () { button.textContent = "Copy"; }, 1200);
      });
    });
    container.appendChild(button);
  }

  function init() {
    document.querySelectorAll(".everest-api details[data-inline]").forEach(watchInline);
    document.querySelectorAll(".everest-api [data-expand]").forEach(function (button) {
      button.addEventListener("click", function () {
        if (button.getAttribute("data-expand") === "all") {
          expandAll();
        } else {
          document.querySelectorAll(".everest-api details").forEach(function (d) { d.open = false; });
        }
      });
    });

    document.querySelectorAll(".everest-api code[data-copy]").forEach(function (code) {
      var holder = document.createElement("span");
      code.parentNode.insertBefore(holder, code.nextSibling);
      addCopyButton(holder, function () { return code.getAttribute("data-copy"); });
    });
    document.querySelectorAll(".everest-api pre[data-copy-block]").forEach(function (pre) {
      addCopyButton(pre, function () { return pre.querySelector("code").textContent; });
    });

    revealHash();
    window.addEventListener("hashchange", revealHash);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
