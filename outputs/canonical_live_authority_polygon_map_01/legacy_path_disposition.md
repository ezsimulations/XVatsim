# Legacy path disposition

| Prior path | Final disposition | Replacement / proof |
|---|---|---|
| `modules/terminal_authority` downloader, compiler, and cache | Deleted | One manifest acquisition and `CanonicalAuthorityMapCompiler` |
| `BrainTerminalAuthorityWorker*` authority-named endpoint packet | Removed and renamed | `BrainEndpointPolygonQuery*`; geometry query only |
| Candidate-scoped vNAS endpoint evidence | Removed | Global `VnasLiveOwnershipSnapshot` is joined before route/radio queries |
| Route-scoped activation order | Removed | Global activation is built before aircraft/route relevance |
| Transceiver geography activation | Removed | Radio facts remain diagnostic/compatibility facts only |
| Duplicated ATIS activation | Removed | ATIS cannot light a polygon |
| First-key terminal geometry deduplication | Removed | Canonical compiler retains every valid source geometry component |
| Independent terminal point-in-polygon implementation | Deleted | Shared topology implementation in `core::authority` |
| Brain endpoint/source-authority vote joins | Removed | Typed live-map assignment plus canonical endpoint containment |
| Harness `arrival`, `departure`, and `enroute` board engines | Deleted | Harness exercises production Brain boards |
| Harness `resolver_enroute` compatibility projection | Removed | Production `BrainControllerRelevanceWorker` board only |
| Legacy rollback switches that could restore vote authority | Deleted | Rollback is source recovery only |

No retained entry in this table can produce or resurrect a live controller
row. The rollback checkpoint restores source state in a separate recovery tree;
there is no runtime switch that re-enables retired authority behavior.

