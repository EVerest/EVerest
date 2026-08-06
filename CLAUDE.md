# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

@AGENTS.md

## Claude Code specifics

Project guidance lives in `AGENTS.md`, imported above, so that every coding agent reads
the same instructions. Put project-wide changes there rather than here.

Personal, uncommitted instructions belong in `CLAUDE.local.md` at the repository root.
It is gitignored and loads after this file.

If you work across several git worktrees of this repository, note that a gitignored
`CLAUDE.local.md` exists only in the worktree where you created it. Keep per-user
preferences in `~/.claude/rules/` instead, which is not per-checkout.
