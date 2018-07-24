# pyaudacity_noisered
Audacity Noise Reduction Python Porting Library. UNIX only.

* reference snapshot
  *  https://github.com/audacity/audacity/tree/848b66f4fae73d1e3236dfe1b402682a01013075


# usage
See test/pyaudacity/test_noisered.py for example.

```python
import pyaudacity

pyaudacity.noisered(profile_path, profile_start, profile_end,
                    src_path, noise_gain, sensitivity, smoothing,
                    dst_path)
```
* profile_path: profile source wave file path
* profile_start: profile range start position(second)
* profile_end: profile range end position(second)
* src_path: input wave file path
* noise_gain: Noise reductioin(dB): The first parameter in Audacity Noise Reduction Step2. 
* sensitivity: Sensitivity: The second parameter in Audacity Noise Reduction Step2.
* smoothing: The third parameter in Audacity Noise Reduction Step2.
* dst_path: output file path

# build
## requirement
* sndfile
* soxr

## command
```
./setup.py build_ext
```
# install
## command
```
./setup.py install
```
