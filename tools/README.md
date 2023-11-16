AVTransport tools
=================

avcat
-----

avcat is a swiss army-knife program, similar to [mkvmerge](https://github.com/skyunix/mkvtoolnix).
It takes one, or many, inputs, and a single output.

If ffmpeg support has been enabled during compilation, either inputs or outputs can be
anything libavformat supports (mkv, mp4, MPEG-TS).

avcat can combine or append inputs, extract streams, add metadata, reconstruct timestamps,
fix errors, and verify integrity.
