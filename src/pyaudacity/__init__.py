import os, sys
sys.path.append(os.path.dirname(__file__))

import cmodule


# pyaudacity_module c extension wrapper
def noisered(profile_path, profile_start, profile_end, src_path, noise_gain, sensitivity, smoothing, dst_path):
    return cmodule.noisered(profile_path, profile_start, profile_end, src_path, noise_gain, sensitivity, smoothing, dst_path)
