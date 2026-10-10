# PlayerCap on the 2026-10-10 smoketest server build

The `smoketest` dedicated-server executable at `D:\BriefcaseAppData\game\versions\smoketest` has SHA-256 `0fec2be320ce099f088316c569a24c91974c518cd4b75479bf069d6b14d29d59`, PE timestamp `0x6AC59711`, and image size `0x05B06000`.

The old PlayerCap startup module in `D:\Projects\Briefcase.PrivateMods\server\Briefcase.PlayerCap.Startup` supports timestamp `0x6A966107` and image size `0x05B60000`. Its manager and session byte patterns each have zero matches in the current smoketest executable, so its offsets and expected original values must not be reused.

Two corresponding calculations are identifiable in the new executable's `.text` section:

| Site | File offset | RVA | Vanilla result |
| --- | ---: | ---: | --- |
| Dedicated-server manager | `0x010CD9E7` | `0x010CE3E7` | Solo 15, Duo 14, Trio 15 |
| EOS/session setup | `0x01357D56` | `0x01358756` | Solo 15, Duo 14, Trio 15 |

The manager's mode branch contains `41 B9 0E 00 00 00` for Duo and `41 B9 0F 00 00 00` for Solo/Trio. The session branch uses `8D 42 0E` for Duo and `B8 0F 00 00 00` for Solo/Trio. Thus the currently installed smoketest binary does **not** have a uniform cap of 15: Duo remains 14, matching the announced limit.

This is research evidence, not a patch specification. Before adapting PlayerCap, verify the mode mapping and any other cap sites, require an exact build identity, locate each site uniquely, and test Solo/Duo/Trio session creation. A configured limit at or below these vanilla ceilings needs no PlayerCap patch on this build.
