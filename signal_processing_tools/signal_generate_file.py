### generate a signal and store it to a file
### plot it in frequency domain
### apply an FIR filter to extract the signal

### reference link: https://www.runoob.com/w3cnote/verilog-fir.html

# dependencies
import math
import numpy as np
from numpy.fft import fft, ifft
from sklearn.preprocessing import MinMaxScaler
import matplotlib.pyplot as plt

# close all previous plots
plt.close('all')

def genCosine(f0, t):
    sinusoid = np.cos(2 * math.pi * f0 * t)
    return sinusoid

def frequencyDomainAnalysis(signal,num_samples,fs):
    x = fft(signal)
    n = np.arange(num_samples)
    delta_f = fs / num_samples
    freq = n * delta_f
    #print(freq)

    plt.figure(figsize=(12, 6))
    plt.subplot(121)
    plt.stem(freq, np.abs(x), 'b', markerfmt=" ", basefmt="-b")
    plt.xlabel('Freq (Hz)')
    plt.ylabel('FFT Amplitude |X(freq)|')

    plt.subplot(122)
    plt.plot(t, ifft(x), 'r')
    plt.xlabel('Time (s)')
    plt.ylabel('Amplitude')
    plt.tight_layout()
    plt.show()
    return

def firExample(signal):

    # apply an FIR filter to filter the noise signal
    # coefficients of the FIR filter
    w = [11,31,63,104,152,198,235,255,235,198,152,104,63,31,11]
    signal_filterd = np.convolve(w,signal)
    plt.plot(signal_filterd[len(w):len(signal)-len(w)])
    plt.show()
    return

if __name__ == '__main__':

    fc = 0.25e6  # center frequency of the signal
    fn = 7.5e6 # frequency of the noise signal
    fs = 50e6  # sampling rate
    T = 1/fc # period of the signal
    num_samples = T*fs  # number of samples
    t = np.arange(num_samples) / fs # discrete time
    signal = genCosine(fc, t)
    noise_signal = genCosine(fn,t)
    #mix_signal = signal
    mix_signal = signal + noise_signal

    #scale the range of data [-1,1]
    mix_signal = np.array(mix_signal).reshape(len(mix_signal),1)
    minmax_scaler = MinMaxScaler(feature_range=(-1, 1))
    mix_signal = minmax_scaler.fit_transform(mix_signal)
    #print(mix_signal)

    #scale the range of data [0,4095]
    mix_signal = np.floor((np.power(2,11)-1)*mix_signal)+np.power(2,11)
    #flatten the array
    mix_signal = np.array(mix_signal).reshape(-1)
    #plt.plot(mix_signal)
    #plt.show()

    #frequencyDomainAnalysis(mix_signal,num_samples,fs)
    #firExample(mix_signal)

    #save the data into a text file
    mix_signal = np.int16(mix_signal)
    np.savetxt('signal.txt',mix_signal,fmt="%x",delimiter="\n")

