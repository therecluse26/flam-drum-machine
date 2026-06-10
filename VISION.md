# FlamKit Vision

> **The stable north star.** This document captures what FlamKit aspires to be and the
> principles every feature is checked against. It is a *direction* document: it makes no
> claims about what is built today. For the current roadmap and feature detail, see
> [README.md](README.md). For how the system is implemented, see
> [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Mission

**Music production should not require financial privilege.**

Professional drum sampling is gated behind $300–$1000+ commercial tools. FlamKit exists to
eliminate that barrier — to give the bedroom producer and the professional studio access to
the *same* world-class drum engine and sample libraries, free forever.

> No one should have to choose between groceries and a drum sampler.

---

## The Problem We Reject

FlamKit is defined as much by what it refuses to accept as by what it builds:

- **Price as a gatekeeper.** $300–$1000+ for a drum sampler treats quality as a luxury good.
  We reject the premise that professional tools must be expensive.
- **Proprietary formats.** Closed, encrypted, undocumented kit formats lock your work into
  one vendor's ecosystem and make it disappear when that vendor does. We reject formats you
  cannot read, edit, or outlive.
- **Platform lock-in.** Tools that run on one OS, one DAW, or one license server fragment
  the community and hold users hostage. We reject single-platform and single-host captivity.

---

## Three Guiding Principles

Each principle is written as a **check you can apply**, not a slogan.

1. **Democratization** — *Does this lower the barrier to professional results, or raise it?*
   Default to the choice that a producer with no budget and modest hardware can still use.
   Anything that helps only well-funded studios fails this check.

2. **Transparency** — *Is the format, the source, and the process open and inspectable?*
   Kit definitions stay human-readable; the code stays open; decisions happen in public
   issues. If a feature requires a black box, it fails this check.

3. **Excellence** — *Would this hold up next to the best commercial tool in its category?*
   "Free" is never an excuse for "compromised." If we cannot meet the professional bar,
   we hold the feature until we can rather than shipping a demo-grade version.

---

## Non-Negotiables

These are fixed commitments. Changing any of them is a change to what FlamKit *is*.

- **Free & open under GPLv3 — strong copyleft.** FlamKit is licensed
  [GPLv3](LICENSE). This is deliberately stronger than the LGPL it replaced: GPLv3 prevents
  both proprietary **forks** and proprietary **linking**. Derivative works — and software
  that links FlamKit's code — must themselves be free and open under GPLv3. The freedom is
  protected for every downstream user, not just the first one.
- **Open, human-readable kit format.** Kits are defined in a documented, text-based format
  you can read, diff, version-control, and edit by hand. No encryption, no lock-in.
- **Real-time safe.** The audio path holds to real-time discipline — no allocations, locks,
  or blocking I/O where they would cause dropouts. Audio correctness is not negotiable for
  any feature.
- **Cross-platform parity.** Linux, macOS, and Windows are first-class and behave
  identically. No platform is a second-class citizen.
- **Efficiency as a feature.** Memory and CPU frugality are design goals, not
  afterthoughts. The target is on the order of tens of MB of RAM per kit — small enough to
  run on modest machines, not just high-end studio rigs.
- **Zero cost, forever.** The engine and the official kits are free with no paywall, no
  "pro tier," and no upsell. This does not change.

---

## What "Best That Exists" Means

FlamKit measures itself against the leading commercial samplers (Superior Drummer, BFD3,
Steven Slate Drums). "Best that exists" is a **bar we hold ourselves to**, expressed as
targets — not a claim about today:

- **Sampling depth** that matches or exceeds commercial libraries: well-distributed velocity
  coverage, generous round-robins to defeat the "machine-gun" effect, and the full set of
  natural articulations and choke behavior.
- **Multi-mic realism** with independent channels routable to separate DAW tracks — a
  virtual multitrack session, the way a real studio recording is mixed.
- **Audio quality** at professional studio specifications (24-bit, 44.1/48 kHz), chosen for
  perceptual transparency rather than spec-sheet vanity.
- **Efficiency that beats the incumbents**, not merely matches them: dramatically lower RAM
  per kit and low CPU at high polyphony, so the quality bar is reachable on ordinary
  hardware.

Where we cannot yet meet a bar, we say so plainly. We do not overclaim. (Status of any given
capability lives in [README.md](README.md) and [ARCHITECTURE.md](ARCHITECTURE.md), never
here.)

---

## Scope Guardrails

This is the section feature proposals are checked against. FlamKit has a deliberately sharp
edge.

**FlamKit IS:**
- A professional-grade, multi-channel **drum engine** and plugin (standalone + VST3/AU/AAX,
  with open formats to follow).
- An **open ecosystem** for sharing high-quality, freely-licensed kits through a distributed,
  no-central-bottleneck distribution model.
- A companion path for *creating* kits (recording, velocity mapping, export) so the
  ecosystem is self-sustaining.

**FlamKit is NOT:**
- **Not a general-purpose sampler or synth.** Staying focused on drums is what lets us be the
  best at drums. Romplers, multisample instruments, and synthesis engines are out of scope.
- **Not a DAW.** We integrate with the user's DAW; we do not try to replace it. Sequencing,
  arrangement, and full mixing live in the host.
- **Not a vector for proprietary or cloud lock-in.** No closed formats, no mandatory accounts,
  no license servers, no single point of control over kit distribution.

A proposal that pulls FlamKit toward "general sampler," "mini-DAW," or "proprietary/cloud
dependency" should be declined or rescoped against this guardrail.

---

## The Decision Test

Before a feature is accepted, it must pass this checklist. A "no" on any line is a reason to
stop and reconsider.

- [ ] **Democratization** — does it lower the barrier to professional results (not raise it)?
- [ ] **Openness** — does it preserve open, human-readable formats and open source?
- [ ] **Real-time safety** — does it respect audio-thread discipline (no dropouts)?
- [ ] **Efficiency** — does it keep us lean on RAM and CPU?
- [ ] **Scope** — does it fit "drum engine + open ecosystem," and avoid drifting into
      general-sampler / DAW / lock-in territory?

> Use this checklist directly in PR review. If a change cannot honestly check every box,
> it needs justification or a rethink before it ships.

---

## North-Star Horizons

Directional themes, **not dated commitments** — the dated roadmap lives in
[README.md](README.md). These describe the *direction of travel*, not a schedule.

- **v1.0 — Professional core.** A drum engine that earns the "professional-grade" claim:
  multi-channel sampling, multi-output routing, efficient streaming, and a small set of
  official kits good enough to stand next to commercial libraries.
- **v1.1 — The open ecosystem.** Distributed, package-manager-style kit repositories so the
  community can publish and discover kits with no central gatekeeper, plus the tooling to
  record and contribute kits.
- **v1.5–v2.0 — Depth and breadth.** Deeper per-channel craft (mixing, effects, sound
  design), richer authoring tools, broader open plugin-format support, and a growing library
  of freely-licensed community kits.
- **v3.0 — Beyond realism.** Creative and hybrid sound design that goes past faithful
  acoustic reproduction — while every one of the principles above still holds.

---

*FlamKit — Free Layered Audio Machine. The open drum engine built for realism, freedom, and
speed. Free forever, under GPLv3.*
