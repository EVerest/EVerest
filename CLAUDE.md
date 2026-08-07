# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

## Claude Code specifics

Project guidance lives in `AGENTS.md`, imported above, so that every coding agent reads
the same instructions. Put project-wide changes there rather than here.

Personal, uncommitted instructions belong in `CLAUDE.local.md` at the repository root;
it is gitignored and loads after this file. Gitignored means per-worktree: it does not
follow you across worktrees. Preferences that should follow you belong in
`~/.claude/CLAUDE.md` or in scoped rule files under `~/.claude/rules/`.
