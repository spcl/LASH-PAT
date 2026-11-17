## Reproducibility Guide

This repository accompanies the paper:

**LASH-PAT: Deadlock-Free Adaptive Shortest-Path Routing for Inter-Chiplet Interconnects**

and contains all code and data required to reproduce the reported results.

### Setup: Compiling BookSim2

```bash
cd booksim2/src
make
```

### Reproducing Experimental Results

To reproduce all results, run:
```bash
python3 main.py -m
```

- Outputs are stored in the `results` directory  
- Runtime is approximately **5 days on a 64-core machine**  
- The script spawns **one thread per core**  

---

### Generating Plots

To generate all plots, run:
```bash
python3 plots.py
```

- Reads input from the `results` directory  
- Stores figures in the `plots` directory  
