"""Patch aqtinstall 3.3.0 for Qt's Windows repository layout introduced in 6.11.

Remove this patch once a released aqtinstall version contains the upstream fix:
https://github.com/miurahr/aqtinstall/issues/1007
"""

from pathlib import Path

import aqt.archives


archives_path = Path(aqt.archives.__file__).resolve()
source = archives_path.read_text(encoding="utf-8")

original = '''    def _arch_ext(self) -> str:
        ext = QtRepoProperty.extension_for_arch(self.arch, self.version >= Version("6.0.0"))
        return ("_" + ext) if ext else ""
'''

replacement = '''    def _arch_ext(self) -> str:
        # Qt 6.11 split Windows desktop metadata into architecture-specific
        # directories such as qt6_6111_mingw. aqtinstall 3.3.0 still requests
        # the former qt6_6111 directory and fails before downloading Qt.
        if self.os_name == "windows" and self.target == "desktop" and self.version >= Version("6.11.0"):
            repository_arches = {
                "win64_mingw": "mingw",
                "win64_llvm_mingw": "llvm_mingw",
                "win64_msvc2022_64": "msvc2022_64",
                "win64_msvc2022_arm64_cross_compiled": "msvc2022_arm64_cross_compiled",
            }
            repository_arch = repository_arches.get(self.arch)
            if repository_arch:
                return "_" + repository_arch
        ext = QtRepoProperty.extension_for_arch(self.arch, self.version >= Version("6.0.0"))
        return ("_" + ext) if ext else ""
'''

if replacement in source:
    print(f"aqtinstall Qt 6.11 compatibility patch already present: {archives_path}")
elif original not in source:
    raise RuntimeError(
        "Refusing to patch an unknown aqtinstall implementation. "
        "Keep aqtinstall pinned to 3.3.0 or update this compatibility patch."
    )
else:
    archives_path.write_text(source.replace(original, replacement, 1), encoding="utf-8")
    print(f"Patched aqtinstall Qt 6.11 repository mapping: {archives_path}")
