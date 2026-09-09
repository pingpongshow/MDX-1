"""Search-coil electromagnetic calculations for the MDX-1 DD coil.

Inductance of a planar multi-turn loop is computed with the Neumann formula on the
loop centre-line, with the bundle's geometric-mean-distance as the self term of each
segment (standard for wound-bundle detector coils; accuracy ~5 %).  Also computes the
overlap position that nulls the TX->RX mutual inductance, winding resistance,
self-resonance and the field / sensitivity figures used in the manual.
"""
import numpy as np, json, sys
mu0 = 4e-7 * np.pi
RHO_CU = 1.72e-8

def d_shape(diameter, x_flat, n=400):
    """Closed D-shaped centre-line: circular arc of the outer circle cut by a vertical
    chord at x = x_flat (loop lies on the side x <= x_flat).  Returns (n,2) points."""
    R = diameter / 2
    a = np.arccos(x_flat / R)          # half-angle of the chord
    ang = np.linspace(a, 2 * np.pi - a, n)   # arc from top chord point round the left to bottom
    arc = np.c_[R * np.cos(ang), R * np.sin(ang)]
    chord = np.c_[np.full(n // 6, x_flat), np.linspace(-R * np.sin(a), R * np.sin(a), n // 6)]
    return np.vstack([arc, chord])

def loop_L(pts, gmd, N):
    """Neumann self inductance of a closed polyline with bundle GMD; times N^2."""
    seg = np.roll(pts, -1, axis=0) - pts
    mid = pts + seg / 2
    L = 0.0
    n = len(pts)
    for i in range(n):
        d = mid - mid[i]
        r = np.hypot(d[:, 0], d[:, 1])
        dot = seg @ seg[i]
        r[i] = 0
        # self term of segment i: straight-wire formula with GMD
        l = np.hypot(*seg[i])
        self_t = 2e-7 * l * (np.log(2 * l / gmd) - 1 + gmd / l) if l > gmd else 0
        mask = r > 0
        L += (mu0 / (4 * np.pi)) * np.sum(dot[mask] / r[mask]) + self_t
    return L * N * N

def mutual(ptsA, ptsB, NA, NB, dz=4e-3):
    """Mutual inductance; the two windings are stacked dz apart (bundle centre to centre)."""
    segA = np.roll(ptsA, -1, axis=0) - ptsA; midA = ptsA + segA / 2
    segB = np.roll(ptsB, -1, axis=0) - ptsB; midB = ptsB + segB / 2
    M = 0.0
    for i in range(len(ptsA)):
        d = midB - midA[i]; r = np.sqrt(d[:, 0] ** 2 + d[:, 1] ** 2 + dz ** 2)
        M += (mu0 / (4 * np.pi)) * np.sum((segB @ segA[i]) / r)
    return M * NA * NB

def bundle_gmd(N, wire_d):
    """GMD of a roughly square bundle of N round wires of diameter wire_d (incl. enamel)."""
    side = wire_d * np.sqrt(N) * 1.1        # 10 % packing slack
    return 0.2235 * (side + side)

def wire_R(length, wire_d_cu):
    return RHO_CU * length / (np.pi * (wire_d_cu / 2) ** 2)

if __name__ == "__main__":
    D = 0.280                      # 11 in coil, outer centre-line diameter of each D
    out = {}
    # ---------- TX D (left half), RX D (right half, mirrored); each D spans past centre by 'overlap'
    for overlap in [0.045]:
        x_flat = overlap                       # TX D extends from -R to +overlap
        tx = d_shape(D, x_flat)
        rx = d_shape(D, x_flat) * np.array([-1, 1])      # mirror for the RX D
        len_tx = np.sum(np.hypot(*(np.roll(tx, -1, axis=0) - tx).T))
        # TX: 0.35 mH target, 0.6 mm (0.5 mm Cu) Litz-equivalent
        for N in range(14, 40):
            L = loop_L(tx, bundle_gmd(N, 0.92e-3), N)
            if L >= 0.35e-3: break
        Ntx = N; Ltx = L
        Rtx = wire_R(len_tx * Ntx, 0.80e-3)
        out["tx"] = dict(turns=Ntx, L_mH=Ltx * 1e3, R_ohm=Rtx, wire="0.80 mm Cu equivalent (Litz 60 x 0.10 mm preferred, or 0.80 mm solid enamelled)", length_m=len_tx * Ntx, perimeter_m=len_tx)
        # RX: 3.0 mH target, 0.25 mm
        for N in range(40, 140):
            L = loop_L(rx, bundle_gmd(N, 0.29e-3), N)
            if L >= 3.0e-3: break
        Nrx = N; Lrx = L
        Rrx = wire_R(len_tx * Nrx, 0.25e-3)
        out["rx"] = dict(turns=Nrx, L_mH=Lrx * 1e3, R_ohm=Rrx, wire="0.25 mm enamelled Cu", length_m=len_tx * Nrx, perimeter_m=len_tx)
        # ---------- null: sweep overlap of the RX D and find zero mutual inductance
        sweep = []
        for ov in np.linspace(-0.045, 0.045, 46):
            rxs = d_shape(D, ov) * np.array([-1, 1])
            M = mutual(tx, rxs, Ntx, Nrx)
            sweep.append((ov * 1e3, M * 1e6))
        sweep = np.array(sweep)
        # find zero crossing
        s = np.sign(sweep[:, 1]); idx = np.where(np.diff(s) != 0)[0]
        null_mm = None
        if len(idx):
            i = idx[0]; x0, y0 = sweep[i]; x1, y1 = sweep[i + 1]; null_mm = x0 - y0 * (x1 - x0) / (y1 - y0)
        out["null"] = dict(rx_overlap_mm=null_mm, sweep=sweep.tolist(), tx_overlap_mm=overlap * 1e3, total_overlap_mm=(None if null_mm is None else null_mm + overlap * 1e3))
        # sensitivity of the null to overlap error (uH per mm) near null
        if null_mm is not None:
            j = np.argmin(np.abs(sweep[:, 0] - null_mm)); slope = (sweep[j + 1, 1] - sweep[j - 1, 1]) / (sweep[j + 1, 0] - sweep[j - 1, 0])
            out["null"]["dM_uH_per_mm"] = slope
            # residual voltage at RX for 1 mm error, at 40.96 kHz with 63 mA peak line current
            out["null"]["residual_mV_per_mm_41k"] = abs(slope) * 1e-6 * 2 * np.pi * 40960 * 0.063 * 1e3
    # ---------- self-resonance of RX (capacitance estimate: winding 30 pF + shield 60 pF + 1 m cable 100 pF)
    Cp = 30e-12 + 60e-12 + 100e-12
    out["rx"]["SRF_kHz"] = 1 / (2 * np.pi * np.sqrt(Lrx * Cp)) / 1e3
    out["rx"]["C_total_pF"] = Cp * 1e12
    # ---------- TX field on axis at depth (Biot-Savart for the D loop), 490 mA peak at 5 kHz line
    def B_axis(pts, N, I, z):
        seg = np.roll(pts, -1, axis=0) - pts; mid = pts + seg / 2
        # field at point (xc, 0, z) above loop centroid
        c = pts.mean(axis=0)
        r = np.c_[c[0] - mid[:, 0], -mid[:, 1], np.full(len(mid), z)]
        dl = np.c_[seg, np.zeros(len(seg))]
        cross = np.cross(dl, r); rn = np.linalg.norm(r, axis=1) ** 3
        return N * I * mu0 / (4 * np.pi) * np.sum(cross / rn[:, None], axis=0)
    out["field_uT"] = {f"{z*100:.0f}cm": float(np.linalg.norm(B_axis(tx, Ntx, 0.49, z)) * 1e6) for z in [0.05, 0.10, 0.20, 0.30, 0.40]}
    json.dump(out, open("coil_calc.json", "w"), indent=1)
    print(json.dumps({k: v for k, v in out.items() if k != "null"}, indent=1))
    if null_mm is not None:
        print("null overlap (RX D flat edge past centre): %.1f mm ; dM/dx = %.2f uH/mm ; residual @41 kHz per mm: %.1f mV" % (out["null"]["rx_overlap_mm"], out["null"]["dM_uH_per_mm"], out["null"]["residual_mV_per_mm_41k"]))
    else:
        for ov, M in sweep[::4]: print("overlap %5.1f mm  M = %8.2f uH" % (ov, M))
