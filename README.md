# KryoFlux Stream Checker

A simple KryoFlux stream checker, with an emphasis on Atari ST (DSDD) floppies.

Purpose: build and do some rudimentary checking of floppy images. Also
visualise floppy surface to eyeball for patterns.

Note: Currently under development; don't consider this ready to use yet.

## Interactive disk viewer

`disk-analysis -n <prefix> -j <dir>` writes `disk.json` plus one
`flux/<side>-<track>.json` per track into `<dir>`. Copy `viewer/index.html`
into that same directory and serve it (e.g. `python3 -m http.server`) to get
a zoomable/pannable radial view of the disk surface, with flux/MFM/filesystem
layer toggles, a per-track revolution selector, and a side selector. Click a
track ring to load its full-resolution flux detail.

