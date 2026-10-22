# QtZpl Designer

Native Qt Widgets editor. Read this file and README.md before modifying it.

- Use Qt 6.11 or newer and the latest supported C++ dialect. Windows builds
  explicitly use MSVC `/std:c++latest`; CMake's MSVC metadata remains CXX23 for
  compatibility with CMake 3.30. For other compilers, CMake selects the newest
  supported standard it recognizes; C++23 support is the minimum prerequisite.
- The sibling `../qt-zpl` owns parsing and rendering. Link `QtZpl::QtZpl` and
  generate every preview through that library. Do not introduce another renderer
  or remote service, and do not modify the sibling checkout as an incidental fix.
- Always build against the current sibling `../qt-zpl` sources unless the user
  explicitly requests another checkout. Never switch to an older snapshot to
  make a build pass. If the dependency is being edited concurrently, verify the
  current sources and report any remaining incompatibility instead of pinning
  an earlier copy.
- The editor owns its mutable document, interaction, ZPL generation, and imported
  image conversion. Library API models remain read-only.
- Preserve the target boundaries: `designer_core` handles values, codecs, images,
  validation, preset adapters, and QtZpl; `designer_session` owns history and file
  lifecycle; `designer_widgets` observes the session and implements interaction.
  Do not move persistence or undo commands back into MainWindow.
- Keep real production INI catalogues outside fixtures. Test adapters with
  synthetic QSettings data, and use `designer_inspect_presets` for explicit local
  corpus checks. Preserve unknown ZPL and source tokens when saving.
- All positions and dimensions are printer dots. Display zoom and monitor DPI
  must never affect exported geometry.
- Unsupported or ambiguous ZPL must remain available in source mode. Do not
  silently discard commands or collapse a multi-label file into one label.
- Use QPalette, a persistent QProxyStyle, QPainter, and delegates. Do not use QSS.
- Read `C:/Users/r.bagautdinov/.codex/WORKFLOW-NOTES.md` before relevant tool,
  Git, publication, or PowerShell actions. Record newly resolved tool problems
  there, including symptom, working solution, and verification.
- During lengthy work, give a brief progress update at least every 30 seconds.
- Batch related implementation changes before running builds and tests.
- Keep agent build trees, temporary probes, screenshots, and other generated
  work inside `build_agent/`; save command logs in `build_agent/logs/`.
  Build helper and CMake presets use `build_agent/build_agent_<config>`.
  Do not create new `build_*` directories or logs in the project root.
- On Windows use `py -3 tools/build_helper.py all --clean --config Debug`
  for final verification. Do not modify Qt Creator build directories.
- Build directories and deployed binaries are generated artifacts and must not
  be committed. Never recursively remove paths outside checked project build
  directories.
- Add focused tests for document round trips, image bit packing, escaping, and
  lossless handling of unsupported ZPL. Render acceptance is against Labelary
  reference images, never go-zpl rendered output.

## Component map

- `src/core/`: values, typed `EditOperation` transforms, codecs, validation and the QtZpl adapter.
- `src/session/`: applied document, source draft/status/revisions, snapshot history and file lifecycle.
- `src/widgets/`: views, UUID selection, source editor adapter and preview coordinator. Debounced
  previews render on a worker thread; `refresh()` stays synchronous for callers that read bounds.
- `src/printing/`: print jobs, profiles and transport. `src/main.cpp` bootstraps the app.
- Widgets submit typed operations to `DocumentSession::execute`; do not reintroduce a copied-document commit path in MainWindow.
- Preview geometry is valid only with its `DocumentRevision`; delayed source applies also check source revision.
- Call QtZpl only through `src/core/qtzpl_adapter.cpp` (`parseZpl`, `parseOptions`, `renderOptions`), which
  applies Designer budgets and parses identical text once. Locate commands with `Command::sourceSpan` and
  `Internal::commandEnd`, fields with `FieldInfo`/`elementFieldRanges`; `Command::source`/`offset` are
  deprecated (QtZpl 0.4.0) and must not be read.
- Barcode values come from `FieldSettings::resolvedBarcode` (QtZpl owns parameter defaults, `^BY`, `^FW`).
  Do not interpret `Barcode::parameters` by position; raw parameters only back lossless-spelling rules.
  Limitations of QtZpl are its diagnostics; Designer rules that depend on them keep a canary test.
- Keep inactive element options compatible with legacy project/clipboard encoding; R6 migration is tracked separately in the plan.
