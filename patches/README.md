# Patches

## resilient-detection (applied)

`cutechess-resilient-detection.patch` — already applied to the source tree
in this repo (see `projects/gui/src/engineconfigurationdlg.{h,cpp}`).

Summary: the engine-configuration dialog used to cache "this engine has
zero UCI/xboard options" as a final answer once detected. That's almost
always a transient failure (cold disk cache, slow first exec, etc.) rather
than a real empty option list, so the dialog now retries detection instead
of trusting a cached empty result.

`resilient-detection-applied.patch` is the same change, captured as a
diff against a pristine vs. patched build tree (kept for provenance/audit).
