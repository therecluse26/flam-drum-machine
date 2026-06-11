# FlamKit website (`/website`)

The v1.0 distribution + marketing site for FlamKit. **Zero build step** — it is plain,
hand-authored static HTML + one CSS file. Open `index.html` in a browser and it just works.

This deliberately has **no toolchain, no `node_modules`, no API keys, and no paid services**,
so it can be hosted for free on GitHub Pages with nothing to install and nothing to break.

## Pages

| File | Page | Notes |
|---|---|---|
| `index.html` | Home | Producer-first hero, value props, developer band |
| `download.html` | Download | Per-platform cards → **GitHub Releases** (`/releases/latest`); SHA-256 verify guide; JSON-LD `SoftwareApplication` schema |
| `docs.html` | Docs | Install · Quick start · Build from source · First plugin |
| `samples.html` | Samples | CC0 kits + provenance/quality bar (populates from FLA-81) |
| `community.html` | Community | GitHub hub + Discord (at launch) + forums |
| `license.html` | License / FAQ | Plain-language GPLv3, CEO-approved copy incl. lawyer caveat |
| `roadmap.html` | Roadmap | Now / Next / Later |
| `404.html` | Not found | — |
| `styles.css` | — | Shared dark, Valhalla-inspired stylesheet |
| `sitemap.xml`, `robots.txt`, `.nojekyll` | — | SEO + Pages config |

All internal links are **relative**, so the site works under any base path
(`/`, `/flam-drum-machine/`, or a custom domain) with no changes.

## Deploy: GitHub Pages (free, recommended — no budget, no secrets)

The board **rejected** the earlier Cloudflare Pages + paid `flam.audio` plan (approval
`99cc82b9`). GitHub Pages needs neither a paid domain nor third-party API tokens.

Two options, either is fine:

**A. Pages from a branch folder (simplest, no Actions secrets):**
1. Ensure the repo is **public** (required for free Pages).
2. Repo → Settings → Pages → Source: *Deploy from a branch* → `main` → `/website`.
3. Live at `https://therecluse26.github.io/flam-drum-machine/` within ~1 minute.

**B. Pages via Actions** (if you prefer building/checks): a standard
`actions/deploy-pages` workflow pointing at `website/` — still free, still no external secrets.

A custom domain (e.g. `flam.audio`) can be added later via Settings → Pages → Custom domain
**if/when a domain budget is approved** — update `sitemap.xml`/`robots.txt` hostnames then.

## Remaining go-live dependencies (not blockers on the site itself)

- **Real download artifacts** — the Download page reads from GitHub Releases. It shows a
  "v1.0 release in progress" notice and links to `/releases/latest` until the CTO publishes
  a signed, checksummed v1.0 release. No site change needed when it lands.
- **Launch kits** — the Samples page populates from FLA-81 (CC0 bridge kits).
- **Discord invite** — Community page swaps the "at launch" note for a live invite once a
  server exists.
