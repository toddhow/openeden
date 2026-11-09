#!/usr/bin/env python3
"""Tests of tools/mwbase_fakes/gen.py (review of 2026-11-08): run with `python tools/mwbase_fakes/test_gen.py`.

The generator must take the pure virtual functions of the named class only, not those of a class that follows it in the
same header, and the committed fakes must be what it generates now.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "gen.py")
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

TWO_CLASSES = """
class First
{
public:
    virtual ~First() = default;
    virtual int one(int a) = 0;
    struct Inner { int x; void helper() { } };
    virtual void three(const std::string& s = {}) = 0;
};

class Second
{
public:
    virtual void two() = 0;
};
"""


def generate(header, cls, base):
    return subprocess.run(
        [sys.executable, GEN, header, cls, base], check=True, capture_output=True, text=True
    ).stdout


def test_second_class_is_not_included():
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "two.hpp")
        with open(path, "w") as f:
            f.write(TWO_CLASSES)
        out = generate(path, "First", "GeneratedFirstBase")
    assert "one(" in out and "three(" in out, out
    assert "two(" not in out, "a pure virtual function of the next class was generated:\n" + out


def test_committed_fakes_are_current():
    fakes = os.path.join(ROOT, "apps", "openmw_tests", "mwmechanics", "fakes")
    cases = [
        ("world", "World", "GeneratedWorldBase"),
        ("luamanager", "LuaManager", "GeneratedLuaManagerBase"),
        ("windowmanager", "WindowManager", "GeneratedWindowManagerBase"),
        ("mechanicsmanager", "MechanicsManager", "GeneratedMechanicsManagerBase"),
    ]
    for file, cls, base in cases:
        header = os.path.join(ROOT, "apps", "openmw", "mwbase", file + ".hpp")
        out = generate(header, cls, base)
        with open(os.path.join(fakes, file + ".inc"), newline="") as f:
            committed = f.read().replace("\r\n", "\n")
        assert out.replace("\r\n", "\n") == committed, file + ".inc differs from the generator output"


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            try:
                fn()
                print("ok   ", name)
            except AssertionError as e:
                failed += 1
                print("FAIL ", name, "-", e)
    sys.exit(1 if failed else 0)
