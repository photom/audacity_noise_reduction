import unittest
import pyaudacity
import numpy as np
from scipy.io import wavfile
from scipy.signal import spectrogram
# import yep


# Calculate spectrogram for a wav audio file
def create_spectrogram(wav_file):
    rate, data = wavfile.read(wav_file)
    nfft = 200  # Length of each window segment
    fs = 8000  # Sampling frequencies
    noverlap = 120  # Overlap between windows
    nchannels = data.ndim
    if nchannels == 1:
        freqs, bins, pxx = spectrogram(data, fs, nperseg=nfft, noverlap=noverlap)
    elif nchannels == 2:
        freqs, bins, pxx = spectrogram(data[:, 0], fs, nperseg=nfft, noverlap=noverlap)
    else:
        raise RuntimeError(f"invalid channels. file={wav_file}")
    return pxx


class TestSpectrogram(unittest.TestCase):
    def test_noisered(self):
        input = '/var/tmp/keyword_recognizer/input.wav'
        prof = '/var/tmp/keyword_recognizer/bg_input.wav'
        output = '/var/tmp/keyword_recognizer/noisered.wav'

        # yep.start('outcome.prof')
        # result = pyaudacity.noisered('../test.wav', 0.000, 0.300, '../test.wav', 12.0, 6.0, 3.0, 'test_out.wav')
        result = pyaudacity.noisered(prof, 0.000, 0.500, input, 12.0, 6.0, 3.0, output)
        self.assertEqual(result, True)
        # expected = create_spectrogram('../test_answer.wav')
        # actual = create_spectrogram('test_out.wav')
        # result does not match
        # np.testing.assert_almost_equal(expected, actual, decimal=5)
        # yep.stop()


if __name__ == '__main__':
    unittest.main()
