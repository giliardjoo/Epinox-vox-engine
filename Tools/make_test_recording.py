# Makes test.wav: five vowels at three pitches with known formants, to check that analyze_voice.py recovers them.
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter
sr=44100
F={"a":(850,1300,2800),"e":(600,2100,2900),"i":(330,2500,3300),"o":(620,950,2700),"u":(350,1000,2600)}
def vowel(f0,forms,dur=3.0):
    n=int(dur*sr); t=np.arange(n)/sr
    ph=2*np.pi*np.cumsum(f0*(1+0.004*np.sin(2*np.pi*5.5*t))/sr)
    src=sum(np.sin(h*ph)/h**1.2 for h in range(1,int(5000/f0)))
    y=src
    for f in forms:
        bw=80+0.05*f; r=np.exp(-np.pi*bw/sr); th=2*np.pi*f/sr
        y=lfilter([1-r],[1,-2*r*np.cos(th),r*r],y)
    env=np.minimum(1,np.minimum(t/0.05,(dur-t)/0.05))
    return y*env
out=[np.zeros(int(0.5*sr))]
for f0 in (110,147,196):
    for v in "aeiou":
        out+= [vowel(f0,F[v]), np.zeros(int(0.6*sr))]
x=np.concatenate(out); x=0.5*x/np.max(np.abs(x))+0.0005*np.random.randn(len(x))
wavfile.write("test.wav",sr,(x*32000).astype(np.int16))
