"""Optimise a 3-level (+V, 0, -V) periodic transmit voltage waveform so that the
coil *voltage* spectrum has equal-magnitude components at the wanted set of
harmonics of the 5 kHz fundamental (5/10/15/20/40 kHz = harmonics 1,2,3,4,8),
zero DC, and minimum energy elsewhere.  Then simulate the coil current for the
specified TX coil and report peak/RMS current and power.

Method: simulated annealing over N time slots per period, each slot in {-1,0,+1}.
"""
import numpy as np, json, sys
rng = np.random.default_rng(7)

F0      = 5000.0          # fundamental (Hz)
T       = 1/F0
N       = 80              # slots per period (2.5 us each -> edge resolution 2.5 us)
WANT    = [1,2,3,4,8]     # harmonics wanted (x5 kHz)
WEIGHT  = {1:1.0,2:1.0,3:1.0,4:1.0,8:1.0}
V_TX    = 12.0            # bridge rail (V) for VLF mode
L_TX    = 0.35e-3         # TX coil inductance (H)
R_TX    = 0.6             # TX coil + wiring + switch resistance (ohm)

def spectrum(w):
    # complex Fourier coefficients c_n (n>=0) of a piecewise-constant waveform
    X = np.fft.rfft(w)/N
    return X

def score(w):
    X = spectrum(w)
    mags = np.abs(X)
    dc = abs(X[0])
    want = np.array([mags[n]*WEIGHT[n] for n in WANT])
    others = mags.copy(); others[WANT]=0; others[0]=0
    # maximise the weakest wanted line, keep them equal, penalise dc and other energy
    return (want.min() - 0.6*(want.max()-want.min()) - 4*dc - 0.15*np.sqrt((others**2).sum()))

def anneal(iters=200000, T0=0.05, T1=0.0005):
    w = rng.integers(-1,2,N)
    best = w.copy(); sb = score(w); s = sb
    for k in range(iters):
        temp = T0*(T1/T0)**(k/iters)
        w2 = w.copy()
        i = rng.integers(N)
        if rng.random()<0.5:
            w2[i] = rng.integers(-1,2)
        else:   # move an edge
            j = (i+1)%N; w2[i],w2[j] = w2[j],w2[i]
        s2 = score(w2)
        if s2>s or rng.random()<np.exp((s2-s)/temp):
            w,s = w2,s2
            if s>sb: best,sb = w.copy(),s
    return best, sb

best=None; sb=-1e9
for trial in range(6):
    w,s = anneal()
    if s>sb: best,sb=w,s
w=best
X=spectrum(w); mags=np.abs(X)
print("score",round(sb,4))
print("slots:", "".join({1:'+',0:'0',-1:'-'}[int(v)] for v in w))
print("DC:",round(abs(X[0]),4))
for n in WANT: print(f"  harmonic {n:2d} ({n*5:2d} kHz): |V_n|/V = {mags[n]:.4f}  -> peak {mags[n]*2*V_TX:.2f} V, phase {np.degrees(np.angle(X[n])):7.1f} deg")
others=[(n,mags[n]) for n in range(1,N//2+1) if n not in WANT]
others.sort(key=lambda t:-t[1])
print("largest unwanted lines:", [(n*5, round(m,3)) for n,m in others[:6]])
# fraction of voltage-squared in wanted lines
tot=(mags[1:]**2).sum(); wanted=sum(mags[n]**2 for n in WANT)
print("wanted-line share of AC voltage energy: %.1f%%"%(100*wanted/tot))

# --- time-domain current simulation for the coil (steady state, exact per-slot solution)
dt=T/N; tau=L_TX/R_TX
v=w*V_TX
# steady state: iterate until periodic
i=0.0; I=np.zeros(N)
for cycle in range(60):
    for k in range(N):
        i = i*np.exp(-dt/tau) + (v[k]/R_TX)*(1-np.exp(-dt/tau))
        I[k]=i
Ipk=np.abs(I).max(); Irms=np.sqrt((I**2).mean()); Pcoil=Irms**2*R_TX
print(f"TX coil {L_TX*1e3:.2f} mH / {R_TX:.2f} ohm at {V_TX:.0f} V rail: Ipeak={Ipk:.2f} A  Irms={Irms:.2f} A  P_loss={Pcoil:.2f} W")
Xi=np.fft.rfft(I)/N
for n in WANT: print(f"  current at {n*5:2d} kHz: {2*abs(Xi[n])*1e3:7.1f} mA peak")
json.dump({"slots":[int(x) for x in w],"N":N,"F0":F0,"V":V_TX,"L":L_TX,"R":R_TX,
           "mag":{str(n):float(mags[n]) for n in WANT},"I":I.tolist()},open("tx_waveform.json","w"))

# --- plot
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
t=np.arange(N)*dt*1e6
fig,ax=plt.subplots(3,1,figsize=(9,8))
ax[0].step(np.r_[t,t[-1]+dt*1e6],np.r_[v,v[-1]],where='post',color='#c0392b'); ax[0].set_ylabel('TX voltage (V)'); ax[0].set_title('Optimised 3-level transmit waveform, one 200 µs period (5 kHz fundamental)')
ax[0].set_ylim(-V_TX*1.3,V_TX*1.3); ax[0].grid(alpha=.3)
ax[1].plot(t,I,color='#2471a3'); ax[1].set_ylabel('TX coil current (A)'); ax[1].grid(alpha=.3); ax[1].set_xlabel('time (µs)')
f=np.arange(N//2+1)*5
ax[2].bar(f,2*mags[:N//2+1]*V_TX,width=3.5,color=['#27ae60' if n in WANT else '#aaaaaa' for n in range(N//2+1)])
ax[2].set_xlim(0,120); ax[2].set_xlabel('frequency (kHz)'); ax[2].set_ylabel('coil voltage (V pk)'); ax[2].grid(alpha=.3)
ax[2].set_title('Voltage spectrum: green = demodulated lines 5/10/15/20/40 kHz')
plt.tight_layout(); plt.savefig("../docs/img/tx_waveform.png",dpi=130)
print("plot saved")
