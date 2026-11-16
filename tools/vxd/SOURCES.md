# PE to LE tools

`pe2le.py` and `lewrite.py` originate in
[ages2001/WINSIMD](https://github.com/ages2001/WINSIMD), revision
`1bcadb374ad5c08c7e5ae80a22b253cd89b9f133`, `SRC/scripts/`.
Their upstream license is GPL-3.0; see COPYING.

Changes for hdparm: configurable DDB/module name, reject PE imports, and retain
only the `write_le` part of lewrite (no repack command or ledump dependency).
The source and notice remain available alongside the optional VxD build.
