# for module compiling
import os
Import('RTT_ROOT')
from building import *

cwd = GetCurrentDir()
objs = []
list = os.listdir(cwd)

ASFLAGS = ' -I' + cwd

for d in list:
    if d == 'core':
        continue
    path = os.path.join(cwd, d)
    if os.path.isfile(os.path.join(path, 'SConscript')):
        objs = objs + SConscript(os.path.join(d, 'SConscript'))

Return('objs')
