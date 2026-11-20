# mk.py <group> <python-expression file>: writes groups/<group>.lua from a python list of dicts (helper for the authors of groups)
import sys, math
def lua(v):
    if isinstance(v, dict): return "{ " + ", ".join("%s = %s" % (k, lua(x)) for k, x in v.items()) + " }"
    if isinstance(v, (list, tuple)): return "{ " + ", ".join(lua(x) for x in v) + " }"
    if isinstance(v, str): return '"%s"' % v
    if isinstance(v, bool): return "true" if v else "false"
    return repr(v)
def write(group, start, shots, extra=None):
    body = 'return { start = "%s", shots = {\n' % start + ",\n".join(lua(s) for s in shots) + "\n} }\n"
    open("groups/%s.lua" % group, "w").write(body)
