# Reference model

I'm using Paramak to build a simple a tokamak for use as non-trivial
input file for regression testing. This is most easily accomplished
using Conda:

```
conda create -n cadquery
conda activate cadquery

conda install -c conda-forge -c cadquery cadquery=master

pip install paramak
```

see [paramak install][] and [cadquery][] for more help. From there you
can run the code from this repo:

1. a Python script to create a simple reactor in STEP format
2. conversion of STEP to BREP format
3. Salome gluer

```
python make_paramak_reactor.py

step_to_brep paramak_reactor.step paramak_reactor.brep

salome_geom_gluer2 paramak_reactor.brep paramak_reactor-salome_glued.brep
```

[cadquery]: https://github.com/CadQuery/cadquery
[paramak install]: https://paramak.readthedocs.io/en/main/install.html
