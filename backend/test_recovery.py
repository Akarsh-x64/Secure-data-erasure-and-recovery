import sys
import os

sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'modules'))

import recovery

print(dir(recovery))
print("Help on recovery:")
help(recovery)
