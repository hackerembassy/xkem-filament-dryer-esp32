#!/usr/bin/env python3
"""
Filament dryer log analysis — auto-detects old (13-col) and new (7-col) CSV formats.

Usage:
    python3 scripts/analyze-log.py <csv_path>
    python3 scripts/analyze-log.py ~/dryer-logs/dryer_2026-04-14_1830.csv

Outputs:
    PNG file alongside input CSV (e.g. dryer_2026-04-14_1830.png)
    Console summary statistics
"""

import sys
import os

# ── Dependency check ──────────────────────────────────────────────────────────
MISSING = []
try:
    import pandas as pd
except ImportError:
    MISSING.append("pandas")
try:
    import numpy as np
except ImportError:
    MISSING.append("numpy")
try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:
    MISSING.append("matplotlib")

if MISSING:
    print(f"ERROR: Missing dependencies: {', '.join(MISSING)}", file=sys.stderr)
    print("Install with: pip install pandas numpy matplotlib", file=sys.stderr)
    sys.exit(1)


# ── Mode enum mapping (matches DryerMode in relay.h) ─────────────────────────
MODE_NAMES = {0: "off", 1: "maintain", 2: "pla", 3: "petg", 4: "abs", 5: "tpu", 6: "mix"}
MODE_NAMES_REV = {v: k for k, v in MODE_NAMES.items()}


def detect_and_load(path):
    """Load CSV, auto-detect format, return normalized DataFrame with standard columns."""
    df = pd.read_csv(path)
    cols = list(df.columns)

    if "ts" in cols and "fl" in cols and "md" in cols:
        # New compact format: ts,ct,hu,ht,fl,sp,md
        out = pd.DataFrame()
        out["timestamp_s"] = df["ts"]
        out["chamber_temp"] = df["ct"]
        out["humidity"] = df["hu"]
        out["heatsink_temp"] = df["ht"]
        out["chamber_valid"] = (df["fl"].astype(int) & 1).astype(bool)
        out["heatsink_valid"] = ((df["fl"].astype(int) >> 1) & 1).astype(bool)
        out["relay_on"] = ((df["fl"].astype(int) >> 2) & 1).astype(int)
        out["lid_open"] = ((df["fl"].astype(int) >> 3) & 1).astype(bool)
        out["overtemp"] = ((df["fl"].astype(int) >> 4) & 1).astype(bool)
        out["thermal_fault"] = ((df["fl"].astype(int) >> 5) & 1).astype(bool)
        out["setpoint"] = df["sp"]
        out["mode_int"] = df["md"].astype(int)
        out["mode"] = out["mode_int"].map(MODE_NAMES).fillna("unknown")
        return out, "new"

    elif "timestamp_ms" in cols:
        # Old 13-column format
        out = pd.DataFrame()
        out["timestamp_s"] = (df["timestamp_ms"] / 1000).astype(int)
        out["chamber_temp"] = df["chamber_temp"]
        out["humidity"] = df["humidity"]
        out["heatsink_temp"] = df["heatsink_temp"]
        out["chamber_valid"] = df["chamber_valid"].astype(bool)
        out["heatsink_valid"] = df["heatsink_valid"].astype(bool)
        out["relay_on"] = df["relay_on"].astype(int)
        out["lid_open"] = df["lid_open"].astype(bool)
        out["overtemp"] = df["overtemp"].astype(bool)
        out["thermal_fault"] = df["thermal_fault"].astype(bool)
        out["setpoint"] = df["setpoint"]
        out["mode"] = df["mode"].astype(str).str.strip()
        out["mode_int"] = out["mode"].map(MODE_NAMES_REV).fillna(-1).astype(int)
        return out, "old"

    else:
        print(f"ERROR: Unrecognized CSV format. Columns: {cols}", file=sys.stderr)
        sys.exit(1)


def detect_sampling_interval(df):
    """Detect sampling interval in seconds from timestamp differences."""
    if len(df) < 2:
        return 2
    diffs = df["timestamp_s"].diff().dropna()
    # Use median to be robust against NTP jumps
    interval = int(round(diffs.median()))
    return max(interval, 1)


def exp_decay(t, a, tau, c):
    return a * np.exp(-t / tau) + c


def fit_humidity(elapsed_h, humidity):
    """Fit exponential decay to humidity data. Returns (popt, r_squared) or None."""
    mask = humidity > 0.5
    t_fit = elapsed_h[mask].values
    h_fit = humidity[mask].values

    if len(t_fit) < 50:
        return None

    try:
        log_h = np.log(h_fit)
        coeffs = np.polyfit(t_fit, log_h, 1)
        slope, intercept = coeffs
        a_fit = np.exp(intercept)
        tau_fit = -1.0 / slope
        c_fit = 0.0

        log_pred = intercept + slope * t_fit
        ss_res = np.sum((log_h - log_pred) ** 2)
        ss_tot = np.sum((log_h - np.mean(log_h)) ** 2)
        r_squared = 1 - ss_res / ss_tot if ss_tot > 0 else 0

        return (a_fit, tau_fit, c_fit), r_squared
    except Exception:
        return None


def detect_dominant_mode(df):
    """Find the most common non-off/maintain mode, falling back to maintain."""
    active = df[~df["mode"].isin(["off", "maintain", "unknown"])]
    if len(active) > 0:
        return active["mode"].mode().iloc[0]
    non_off = df[df["mode"] != "off"]
    if len(non_off) > 0:
        return non_off["mode"].mode().iloc[0]
    return df["mode"].mode().iloc[0]


def find_mode_transitions(df):
    """Return list of (elapsed_h, from_mode, to_mode) for mode changes."""
    transitions = []
    prev = df["mode"].iloc[0]
    for idx in range(1, len(df)):
        cur = df["mode"].iloc[idx]
        if cur != prev:
            transitions.append((df["elapsed_h"].iloc[idx], prev, cur))
            prev = cur
    return transitions


def print_summary(df, dominant, interval_s, fit_result, transitions):
    """Print console summary statistics."""
    duration_h = df["elapsed_h"].max()

    # Detect setpoint from data
    setpoints = df["setpoint"].unique()
    active_sp = df[df["setpoint"] > 0]["setpoint"]
    main_setpoint = active_sp.mode().iloc[0] if len(active_sp) > 0 else 0

    # Steady-state: after chamber first reaches setpoint - 2
    ss_threshold = main_setpoint - 2.0
    ss_rows = df[df["chamber_temp"] >= ss_threshold]
    has_ss = len(ss_rows) > 0 and main_setpoint > 0

    print("=" * 60)
    print(f"  FILAMENT DRYER LOG ANALYSIS")
    print(f"  Mode: {dominant.upper()}  |  Setpoint: {main_setpoint:.0f}\u00b0C  |  Interval: {interval_s}s")
    print("=" * 60)
    print(f"\n{'Duration:':<30} {duration_h:.2f} hours ({len(df)} samples)")

    if has_ss:
        ss_start_h = ss_rows["elapsed_h"].iloc[0]
        ss = df[df["elapsed_h"] >= ss_start_h]
        print(f"{'Heating ramp time:':<30} {ss_start_h * 60:.1f} minutes")
        print()
        print("-- Steady-State Chamber Temperature --")
        print(f"{'  Mean:':<30} {ss['chamber_temp'].mean():.1f}\u00b0C")
        print(f"{'  Std dev:':<30} {ss['chamber_temp'].std():.2f}\u00b0C")
        print(f"{'  Range:':<30} {ss['chamber_temp'].min():.1f} -- {ss['chamber_temp'].max():.1f}\u00b0C")
        print()
        print("-- Heatsink Temperature --")
        print(f"{'  Steady-state mean:':<30} {ss['heatsink_temp'].mean():.1f}\u00b0C")
        print(f"{'  Peak:':<30} {ss['heatsink_temp'].max():.1f}\u00b0C")
        delta = ss["heatsink_temp"] - ss["chamber_temp"]
        print(f"{'  Mean delta (HS-chamber):':<30} {delta.mean():.1f}\u00b0C")
        print()
        print("-- Relay / Energy --")
        print(f"{'  Steady-state duty cycle:':<30} {ss['relay_on'].mean() * 100:.1f}%")
        ss_relay_on_s = ss["relay_on"].sum() * interval_s
    else:
        print()

    total_relay_on_s = df["relay_on"].sum() * interval_s
    print(f"{'  Overall duty cycle:':<30} {df['relay_on'].mean() * 100:.1f}%")
    print(f"{'  Total relay ON time:':<30} {total_relay_on_s / 60:.0f} min ({total_relay_on_s / 3600:.1f} h)")

    print()
    print("-- Humidity --")
    humidity_initial = df["humidity"].iloc[:max(1, 30)].mean()
    humidity_final = df["humidity"].iloc[-max(1, 30):].mean()
    print(f"{'  Initial (first min avg):':<30} {humidity_initial:.1f}%")
    print(f"{'  Final (last min avg):':<30} {humidity_final:.1f}%")

    if fit_result is not None:
        popt, r_sq = fit_result
        a, tau, c = popt
        print(f"{'  Exponential fit:':<30} {a:.1f}\u00b7exp(-t/{tau:.2f}h)")
        print(f"{'  Time constant (tau):':<30} {tau:.2f} hours ({tau * 60:.0f} min)")
        print(f"{'  R\u00b2 (log-space):':<30} {r_sq:.4f}")

        for target in [5.0, 2.0, 1.0, 0.5]:
            if a > target > 0:
                t_target = -tau * np.log(target / a)
                if t_target > 0:
                    status = "reached" if t_target <= duration_h else f"would need {t_target:.1f}h"
                    print(f"{'  Time to ' + str(target) + '%:':<30} {t_target:.1f}h -- {status}")

    if transitions:
        print()
        print("-- Mode Transitions --")
        for t_h, from_m, to_m in transitions:
            print(f"  {t_h:.2f}h: {from_m} -> {to_m}")

    print("=" * 60)


def plot_analysis(df, dominant, interval_s, fit_result, transitions, out_path):
    """Generate 4-panel analysis plot."""
    duration_h = df["elapsed_h"].max()
    active_sp = df[df["setpoint"] > 0]["setpoint"]
    main_setpoint = active_sp.mode().iloc[0] if len(active_sp) > 0 else 0

    # Rolling window: 5 minutes worth of samples
    window = max(1, int(300 / interval_s))

    # Steady-state detection
    ss_threshold = main_setpoint - 2.0
    ss_rows = df[df["chamber_temp"] >= ss_threshold]
    has_ss = len(ss_rows) > 0 and main_setpoint > 0
    ss_start_h = ss_rows["elapsed_h"].iloc[0] if has_ss else None

    t = df["elapsed_h"].values

    fig, axes = plt.subplots(4, 1, figsize=(14, 16), sharex=True,
                              gridspec_kw={"hspace": 0.08})
    fig.suptitle(f"Filament Dryer Analysis \u2014 {dominant.upper()} @ {main_setpoint:.0f}\u00b0C "
                 f"({duration_h:.1f}h, {len(df)} samples)",
                 fontsize=16, fontweight="bold", y=0.98)

    # ── Panel 1: Temperatures ────────────────────────────────────────────────
    ax1 = axes[0]
    ax1.plot(t, df["chamber_temp"], color="#e74c3c", linewidth=0.8, alpha=0.9, label="Chamber")
    ax1.plot(t, df["heatsink_temp"], color="#e67e22", linewidth=0.6, alpha=0.7, label="Heatsink")
    if main_setpoint > 0:
        ax1.axhline(y=main_setpoint, color="#2c3e50", linestyle="--", linewidth=1.2,
                     alpha=0.8, label=f"Setpoint ({main_setpoint:.0f}\u00b0C)")
    if ss_start_h is not None:
        ax1.axvline(x=ss_start_h, color="#95a5a6", linestyle=":", linewidth=1,
                     alpha=0.6, label=f"Steady-state ({ss_start_h * 60:.0f} min)")

    for t_h, _, to_m in transitions:
        ax1.axvline(x=t_h, color="#27ae60", linestyle="-.", linewidth=0.8, alpha=0.5)

    ax1.set_ylabel("Temperature (\u00b0C)", fontsize=11)
    ax1.legend(loc="right", fontsize=9)
    ax1.set_ylim(15, max(110, df["heatsink_temp"].max() + 10))
    ax1.grid(True, alpha=0.3)

    if has_ss:
        ss = df[df["elapsed_h"] >= ss_start_h]
        ax1.set_title(f"Chamber: {ss['chamber_temp'].mean():.1f}\u00b1{ss['chamber_temp'].std():.1f}\u00b0C (SS)  |  "
                       f"Heatsink peak: {ss['heatsink_temp'].max():.0f}\u00b0C",
                       fontsize=10, loc="left", color="#555")

    # ── Panel 2: Humidity ────────────────────────────────────────────────────
    ax2 = axes[1]
    ax2.plot(t, df["humidity"], color="#3498db", linewidth=0.8, alpha=0.9, label="Humidity")

    humidity_initial = df["humidity"].iloc[:max(1, 30)].mean()
    humidity_final = df["humidity"].iloc[-max(1, 30):].mean()

    if fit_result is not None:
        popt, r_sq = fit_result
        a, tau, c = popt
        t_fit_line = np.linspace(0, duration_h, 500)
        h_fit_line = exp_decay(t_fit_line, *popt)
        ax2.plot(t_fit_line, h_fit_line, color="#2c3e50", linestyle="--", linewidth=1.5,
                 alpha=0.8, label=f"Fit: {a:.1f}\u00b7e^(-t/{tau:.2f}h)")

        # Extrapolation beyond data
        t_ext = np.linspace(duration_h, duration_h * 1.3, 200)
        h_ext = np.maximum(exp_decay(t_ext, *popt), 0)
        ax2.plot(t_ext, h_ext, color="#2c3e50", linestyle=":", linewidth=1.2,
                 alpha=0.5, label="Extrapolation")

        # Target markers
        for target in [5.0, 2.0, 1.0, 0.5]:
            if a > target > 0:
                t_target = -tau * np.log(target / a)
                if 0 < t_target <= duration_h * 1.5:
                    ax2.axhline(y=target, color="#95a5a6", linestyle=":", linewidth=0.8, alpha=0.5)
                    ax2.annotate(f"{target}% @ {t_target:.1f}h", xy=(t_target, target),
                                fontsize=8, color="#555", ha="left", va="bottom")

        title_str = (f"Humidity: {humidity_initial:.1f}% \u2192 {humidity_final:.1f}%  |  "
                     f"\u03c4 = {tau * 60:.0f} min ({tau:.1f}h)  |  R\u00b2 = {r_sq:.3f} (log)")
    else:
        title_str = f"Humidity: {humidity_initial:.1f}% \u2192 {humidity_final:.1f}%"

    ax2.set_ylabel("Relative Humidity (%)", fontsize=11)
    ax2.legend(loc="upper right", fontsize=9)
    ax2.set_ylim(-1, max(25, humidity_initial + 5))
    ax2.grid(True, alpha=0.3)
    ax2.set_title(title_str, fontsize=10, loc="left", color="#555")

    # ── Panel 3: Relay Duty Cycle ────────────────────────────────────────────
    ax3 = axes[2]
    relay_duty = df["relay_on"].rolling(window=window, center=True, min_periods=1).mean() * 100
    ax3.fill_between(t, relay_duty, color="#e74c3c", alpha=0.15)
    ax3.plot(t, relay_duty, color="#e74c3c", linewidth=1, alpha=0.8,
             label="Duty cycle (5-min rolling)")
    overall_duty = df["relay_on"].mean() * 100
    ax3.axhline(y=overall_duty, color="#2c3e50", linestyle="--", linewidth=1, alpha=0.6,
                label=f"Overall: {overall_duty:.0f}%")
    if ss_start_h is not None:
        ax3.axvline(x=ss_start_h, color="#95a5a6", linestyle=":", linewidth=1, alpha=0.6)

    for t_h, _, to_m in transitions:
        ax3.axvline(x=t_h, color="#27ae60", linestyle="-.", linewidth=0.8, alpha=0.5)

    ax3.set_ylabel("Relay Duty Cycle (%)", fontsize=11)
    ax3.legend(loc="upper right", fontsize=9)
    ax3.set_ylim(-5, 105)
    ax3.grid(True, alpha=0.3)

    total_on_min = df["relay_on"].sum() * interval_s / 60
    ax3.set_title(f"Overall duty: {overall_duty:.0f}%  |  Total ON: {total_on_min:.0f} min",
                  fontsize=10, loc="left", color="#555")

    # ── Panel 4: Thermal Delta ───────────────────────────────────────────────
    ax4 = axes[3]
    temp_delta = df["heatsink_temp"] - df["chamber_temp"]
    ax4.plot(t, temp_delta, color="#9b59b6", linewidth=0.6, alpha=0.7,
             label="\u0394T (heatsink \u2212 chamber)")
    delta_smooth = temp_delta.rolling(window=window, center=True, min_periods=1).mean()
    ax4.plot(t, delta_smooth, color="#8e44ad", linewidth=1.5, alpha=0.9, label="5-min rolling avg")

    if has_ss:
        ss = df[df["elapsed_h"] >= ss_start_h]
        ss_delta = (ss["heatsink_temp"] - ss["chamber_temp"]).mean()
        ax4.axhline(y=ss_delta, color="#2c3e50", linestyle="--", linewidth=1, alpha=0.6,
                    label=f"SS mean: {ss_delta:.1f}\u00b0C")
        ax4.set_title(f"Thermal coupling: mean \u0394T = {ss_delta:.1f}\u00b0C  |  "
                       f"peak \u0394T = {(ss['heatsink_temp'] - ss['chamber_temp']).max():.1f}\u00b0C",
                       fontsize=10, loc="left", color="#555")

    if ss_start_h is not None:
        ax4.axvline(x=ss_start_h, color="#95a5a6", linestyle=":", linewidth=1, alpha=0.6)

    ax4.set_ylabel("\u0394T (\u00b0C)", fontsize=11)
    ax4.set_xlabel("Elapsed Time (hours)", fontsize=12)
    ax4.legend(loc="upper right", fontsize=9)
    ax4.grid(True, alpha=0.3)

    # ── Final touches ────────────────────────────────────────────────────────
    for ax in axes:
        ax.tick_params(labelsize=10)
        for t_h, _, to_m in transitions:
            ax.axvline(x=t_h, color="#27ae60", linestyle="-.", linewidth=0.8, alpha=0.4)

    x_max = duration_h * 1.05
    # Widen slightly if humidity extrapolation goes further
    if fit_result is not None:
        x_max = max(x_max, duration_h * 1.35)
    for ax in axes:
        ax.set_xlim(-0.05, x_max)

    plt.tight_layout(rect=[0, 0, 1, 0.97])
    plt.savefig(out_path, dpi=150, bbox_inches="tight", facecolor="white")
    plt.close()
    print(f"\nPlot saved to {out_path}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 analyze-log.py <csv_path>", file=sys.stderr)
        sys.exit(1)

    csv_path = sys.argv[1]
    if not os.path.isfile(csv_path):
        print(f"ERROR: File not found: {csv_path}", file=sys.stderr)
        sys.exit(1)

    # Output PNG alongside CSV
    base, _ = os.path.splitext(csv_path)
    png_path = base + ".png"

    # Load and normalize
    df, fmt = detect_and_load(csv_path)
    print(f"Loaded {len(df)} records from {csv_path} ({fmt} format)")

    if len(df) < 2:
        print("ERROR: Not enough data to analyze (need at least 2 records)", file=sys.stderr)
        sys.exit(1)

    # Compute elapsed time
    interval_s = detect_sampling_interval(df)
    df["elapsed_h"] = (df["timestamp_s"] - df["timestamp_s"].iloc[0]) / 3600.0

    # Auto-detect dominant mode
    dominant = detect_dominant_mode(df)

    # Find mode transitions
    transitions = find_mode_transitions(df)

    # Humidity exponential fit
    fit_result = fit_humidity(df["elapsed_h"], df["humidity"])

    # Console summary
    print_summary(df, dominant, interval_s, fit_result, transitions)

    # Plot
    plot_analysis(df, dominant, interval_s, fit_result, transitions, png_path)


if __name__ == "__main__":
    main()
