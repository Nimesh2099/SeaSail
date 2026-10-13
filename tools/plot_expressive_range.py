"""Plots the CSV written by ExpressiveRange (see "Evaluation" in README.md).

Usage: python tools/plot_expressive_range.py [analysis/expressive_range.csv] [docs/analysis]
Needs pandas and matplotlib.
"""
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

src = sys.argv[1] if len(sys.argv) > 1 else "analysis/expressive_range.csv"
out = sys.argv[2] if len(sys.argv) > 2 else "docs/analysis"
os.makedirs(out, exist_ok=True)

SURFACE, INK, INK2, GRID = "#fcfcfb", "#1f1f1e", "#5f5e5a", "#e4e3df"
TYPES = [("mountain", "Mountain", "#2a78d6"), ("atoll", "Atoll", "#eb6834"),
         ("stacks", "Sea stacks", "#1baf7a"), ("cay", "Cay", "#eda100")]
BLUES = ["#cde2fb", "#9ec5f4", "#6da7ec", "#3987e5", "#2a78d6", "#1c5cab", "#104281"]

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": INK2, "xtick.color": INK2, "ytick.color": INK2,
    "text.color": INK, "font.size": 11, "axes.titlesize": 13, "axes.titleweight": "bold",
    "axes.titlelocation": "left", "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
    "axes.spines.top": False, "axes.spines.right": False, "axes.axisbelow": True,
})

d = pd.read_csv(src)
er = d[d.erosion == 1]

# 1. Expressive range of the whole generator: size vs height, by island type.
fig, ax = plt.subplots(figsize=(9, 6))
for key, label, col in TYPES:
    t = er[er.type == key]
    ax.scatter(t.land_area_km2 * 1e6, t.peak_m, s=22, color=col, alpha=0.75,
               edgecolors=SURFACE, linewidths=0.8, label=f"{label} (n={len(t)})")
    mx, my = np.median(t.land_area_km2 * 1e6), np.median(t.peak_m)
    ax.annotate(label, (mx, my), xytext=(10, 10), textcoords="offset points", color=INK,
                fontweight="bold", fontsize=11)
ax.set_xscale("log")
ax.set_yscale("log")
ax.set_xlabel("Land area above sea level (m², log scale)")
ax.set_ylabel("Peak height (m, log scale)")
ax.set_title(f"Expressive range: {len(er)} islands from {er.seed.nunique()} seeds")
ax.legend(frameon=False, loc="upper left", fontsize=10)
fig.tight_layout()
fig.savefig(os.path.join(out, "fig1_expressive_range.png"), dpi=150)
plt.close(fig)

# 2. Mountain islands only: a Smith & Whitehead style heatmap of two output metrics.
m = er[er.type == "mountain"]
fig, ax = plt.subplots(figsize=(8, 6))
from matplotlib.colors import ListedColormap
cmap = ListedColormap(BLUES)
cmap.set_under(SURFACE)
h = ax.hist2d(m.steep_share * 100, m.ruggedness_m, bins=[18, 18], cmap=cmap, vmin=1)
cb = fig.colorbar(h[3], ax=ax)
cb.set_label("Islands per bin", color=INK2)
cb.outline.set_visible(False)
ax.grid(False)
ax.set_xlabel("Land steeper than 35° (%)")
ax.set_ylabel("Ruggedness (mean height step to neighbours, m)")
ax.set_title(f"Mountain islands (n={len(m)}): how steep vs how broken-up")
fig.tight_layout()
fig.savefig(os.path.join(out, "fig2_mountain_heatmap.png"), dpi=150)
plt.close(fig)

# 3. What erosion changes, per mountain island (paired: same island with and without).
mt = d[d.type == "mountain"].pivot_table(index=["seed", "cell_x", "cell_z"], columns="erosion",
                                         values=["mean_land_height_m", "peak_m", "land_area_km2",
                                                 "mean_slope_deg", "ruggedness_m", "steep_share"])
metrics = [("mean_land_height_m", "Mean land height"), ("peak_m", "Peak height"),
           ("land_area_km2", "Land area"), ("mean_slope_deg", "Mean slope"),
           ("ruggedness_m", "Ruggedness"), ("steep_share", "Land steeper than 35°")]
rows = []
for k, label in metrics:
    base = mt[k][0].replace(0, np.nan)
    ch = ((mt[k][1] - mt[k][0]) / base * 100).dropna()
    rows.append((label, ch.median(), ch.quantile(0.25), ch.quantile(0.75), (ch > 0).mean() * 100, len(ch)))
fig, ax = plt.subplots(figsize=(9, 4.8))
ys = np.arange(len(rows))[::-1]
for y, (label, med, q1, q3, up, n) in zip(ys, rows):
    ax.plot([q1, q3], [y, y], color="#86b6ef", linewidth=6, solid_capstyle="round")
    ax.scatter([med], [y], s=70, color="#2a78d6", edgecolors=SURFACE, linewidths=2, zorder=3)
    ax.annotate(f"{med:+.1f}%", (med, y), xytext=(0, 11), textcoords="offset points",
                ha="center", color=INK, fontsize=10, fontweight="bold")
ax.axvline(0, color=INK2, linewidth=1)
ax.set_ylim(-0.6, len(rows) - 0.3)
ax.set_yticks(ys)
ax.set_yticklabels([r[0] for r in rows])
ax.grid(axis="y", visible=False)
ax.set_xlabel("Change after erosion, % (dot = median, bar = middle 50% of islands)")
ax.set_title(f"Erosion carves valleys but keeps the summits ({rows[0][5]} mountain islands)")
fig.tight_layout()
fig.savefig(os.path.join(out, "fig3_erosion_effect.png"), dpi=150)
plt.close(fig)

# Summary table (Markdown) for the README.
lines = ["| Type | Islands | Land area (m², median) | Peak (m, median) | Mean slope (°) | Ruggedness (m) | Coast index |",
         "|---|---|---|---|---|---|---|"]
for key, label, _ in TYPES:
    t = er[er.type == key]
    lines.append(f"| {label} | {len(t)} | {t.land_area_km2.median() * 1e6:,.0f} | {t.peak_m.median():.1f} | "
                 f"{t.mean_slope_deg.median():.1f} | {t.ruggedness_m.median():.2f} | {t.coast_index.median():.2f} |")
lines += ["", "| Erosion change (mountain islands) | Median | Middle 50% | Islands where it rose |", "|---|---|---|---|"]
for label, med, q1, q3, up, n in rows:
    lines.append(f"| {label} | {med:+.1f}% | {q1:+.1f}% to {q3:+.1f}% | {up:.0f}% |")
t = d[(d.type == "mountain")]
lines += ["", f"Median time to sample one mountain island at 4 m: {t[t.erosion == 0].sample_ms.median():.0f} ms raw, "
          f"{t[t.erosion == 1].sample_ms.median():.0f} ms with erosion (includes the one-off erosion run)."]
with open(os.path.join(out, "summary.md"), "w") as f:
    f.write("\n".join(lines) + "\n")
print("\n".join(lines))
