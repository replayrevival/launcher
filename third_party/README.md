# Vendored dependencies

Sources extracted unchanged from the release archives pinned in `cmake/Deps.cmake` (the archive SHA-256 is checked when they are downloaded instead). Licences and use: see `CREDITS.md`.

| Folder | Project | Version | Licence | Archive SHA-256 |
|---|---|---|---|---|
| SDL3 | SDL | 3.4.18 | zlib | 9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3 |
| RmlUi | RmlUi | 6.3 | MIT | d977298bb6147610e5984d5db85ddf284020d655a8713913f6982074f1dbdede |
| Freetype | FreeType | 2.15.0 | FTL or GPLv2 (GPLv2 used) | 93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5 |
| nlohmann_json | nlohmann/json | 3.12.0 | MIT | 4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187 |
| miniz | miniz | 3.1.2 | MIT | 98468f8924934b723276680f85238b6c78bf1f8b49b4459cc9b7214a20e2e9fb |

Do not edit these folders. To update one, change the URL and hash in `cmake/Deps.cmake`, extract the new archive over the folder, and update this table and `CREDITS.md`.
