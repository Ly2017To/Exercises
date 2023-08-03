### starter code of signal processing
### signal generation and plot
### FFT and IFFT

#dependencies
import math
import numpy as np
from numpy.fft import fft, ifft
#from sklearn import preprocessing
import matplotlib.pyplot as plt

#close all previous plots
plt.close('all')

def genSine(f0,t):
    sinusoid = np.sin(2*math.pi*f0*t)
    return sinusoid

if __name__ == '__main__':
    f0 = 440 #frequency of the signal
    fs = 16000 #sampling rate
    num_samples = 8000 #number of samples
    t = np.arange(num_samples)/fs
    sinusoid = genSine(f0,t)
    #plt.plot(sinusoid)

    x = fft(sinusoid)
    n = np.arange(num_samples)
    delta_f = fs / num_samples
    freq = n * delta_f
    #print(freq)

    plt.figure(figsize=(12, 6))
    plt.subplot(121)
    plt.stem(freq, np.abs(x), 'b', markerfmt=" ", basefmt="-b")
    plt.xlabel('Freq (Hz)')
    plt.ylabel('FFT Amplitude |X(freq)|')
    plt.xlim(0, 1000)

    plt.subplot(122)
    plt.plot(t, ifft(x), 'r')
    plt.xlabel('Time (s)')
    plt.ylabel('Amplitude')
    plt.tight_layout()
    plt.show()
