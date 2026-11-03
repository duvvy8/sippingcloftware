# Windows gaming clipper

Implementation in progress. No product name or branding is assigned. The complete
acceptance checklist is [docs/IMPLEMENTATION_CHECKLIST.md](docs/IMPLEMENTATION_CHECKLIST.md).
Do not treat a development build as a completed release.

Build on Windows x64 with Visual Studio Desktop C++ tools, Windows SDK and CMake:

```powershell
./scripts/bootstrap.ps1
./scripts/build.ps1
```

Bootstrap uses Python only to acquire Qt development libraries. Neither Python nor
Visual Studio is required by the distributed application. An existing matching
dependency cache can be supplied with `bootstrap.ps1 -CacheRoot <directory>`.

Dependencies are specified in `dependencies.lock.json`. Qt is dynamically linked.
FFmpeg includes GPL codecs; distribution must include the matching source and
licence notices. See `licenses/` and the forthcoming distribution report.

The specification is preserved verbatim in `spec/IMPLEMENTATION_PLAN.md`.
