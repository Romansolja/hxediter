# Security model

This document describes the threat model for HxEditer. It is meant
for users evaluating whether to install the app on a managed system,
and for contributors who want to understand the trust assumptions
before proposing changes.

## Reporting a vulnerability

Please open a GitHub issue for non-sensitive findings. For anything
that could be weaponised before a release lands, contact the
maintainer privately via the email on the GitHub profile page so we
can coordinate a fix and a release before public disclosure.

## What HxEditer does and does not protect

HxEditer is a single-user, local hex editor for macOS. It does not
run a network listener, does not collect telemetry, does not load
remote content into the editor view, and makes no outbound network
requests of its own.

The editor opens user-selected files with shared read+write access so
external tools can keep saving over the same file. Concurrent external
writes are detected via an mtime+size token and surfaced in a conflict
dialog. None of this is a defence against malicious files — opening
an attacker-supplied file is no more dangerous than opening it in any
other editor, but no less either.

## Update model

**No auto-updater on macOS.** The macos-beta build ships without
the in-app updater. Users update by re-downloading the DMG from
the [Releases](https://github.com/Romansolja/hxediter/releases)
page; the published `SHA256SUMS.txt` matches the DMG bytes for
manual verification (`shasum -a 256 HxEditer-X.Y.Z-macos-arm64.dmg`).
