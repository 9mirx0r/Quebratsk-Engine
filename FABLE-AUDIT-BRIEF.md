# Brief de auditoría para Fable — Quebratsk Engine y la GoldSrc Encyclopedia

**Destinatario:** Fable 5.2, como auditor externo del proyecto.
**Repositorio:** `https://github.com/9mirx0r/Quebratsk-Engine` (checkout local en
`D:\Proyectos\Quebratsk Engine`, working tree limpio al momento de este brief).
**Commits a auditar:** `a1ac8f4..2e578bb` (ver §1). El HEAD es `2e578bb`.

---

## Lo que esperamos de esta auditoría (leé esto primero)

No queremos solamente un veredicto. Queremos las cinco cosas, en este orden:

1. **Veredicto por área** — tabla con grados y una línea de justificación, al estilo de
   `AUDIT-2026-11-11.md`, actualizada al commit actual.
2. **Qué hacemos bien** — con evidencia concreta (archivo, línea, medición), para
   reforzarlo y no romperlo.
3. **Qué hacemos mal** — con evidencia concreta y la explicación de POR QUÉ está mal.
4. **Ideas de mejora concretas y fundamentadas** — en cualquier aspecto (arquitectura,
   testing, documentación, CI, proceso, tooling, metodología de la iniciativa). Si
   proponés una técnica, patrón, herramienta o práctica, indicá de dónde sale (libro,
   paper, práctica de industria, proyecto de referencia) para que podamos validarla
   nosotros. Preferimos tres recomendaciones sólidas y verificables antes que veinte
   genéricas.
5. **Plan priorizado de siguientes pasos** — qué conviene hacer ya, qué después, y qué
   convendría NO hacer.

---

## 1. Alcance y línea de base

La auditoría cubre dos planos:

| Plano | Qué es | Dónde está |
|---|---|---|
| El proyecto entero | GDExtension C++23 para Godot que lee assets GoldSrc/Source + lab jugable | `README.md`, `CHANGELOG.md`, `src/`, `demo/` |
| La iniciativa nueva | "GoldSrc Encyclopedia": enciclopedia de formatos GoldSrc verificada contra el corpus retail | `docs/GOLDSRC-ENCYCLOPEDIA.md`, `docs/encyclopedia/`, `tools/`, `data/` |

**Línea de base:** `AUDIT-2026-11-11.md` es una auditoría previa (realizada por otro
asistente) sobre el commit `a1ac8f4`, que es exactamente el commit anterior a los seis
nuevos. Compará tu lectura contra esa: qué de sus críticas sigue vigente, cuáles fueron
atendidas (directa o indirectamente) y qué cambió desde entonces. Sus hallazgos más
fuertes: el pipeline parser/IR/converter está bien arquitecturado; las suites GDScript
salen 0 sin verificar nada; el mayor gap de GoldSrc son los lightmaps (lump 8); el
último release no contiene el trabajo de `main`.

**Commits nuevos (el objeto principal de esta auditoría):**

| Commit | Contenido |
|---|---|
| `817c48c` | chore: ignora árbol de build Ninja y caches de Python |
| `66fe5d2` | docs: plan de la enciclopedia (`docs/GOLDSRC-ENCYCLOPEDIA.md`), kickoff, audit previo |
| `7fd5fa1` | feat: Stage 0 — scanner de corpus determinista + inventario de 22 carpetas de juego |
| `93fcb54` | feat: Stage 1 — cuatro validadores de formato, structs pineados, docs de formato y gate |
| `11e0d74` | fix: el parser SPR no salteaba el int32 de tipo por frame; fix + test + verificación full-corpus |
| `2e578bb` | chore: archivos `.uid` de Godot |

---

## 2. Cómo empezar a trabajar acá (orden de lectura sugerido)

1. `README.md` — qué es el proyecto y cómo se construye.
2. `docs/GOLDSRC-ENCYCLOPEDIA.md` — el plan completo de la iniciativa (Stages 0–6,
   reglas de evidencia, las 15 preguntas iniciales). Es EL documento a auditar.
3. `AUDIT-2026-11-11.md` — la auditoría previa (línea de base).
4. `KICKOFF-GOLDSRC-ENCYCLOPEDIA.md` — estado de la máquina: Steam en `C:\Steam}`,
   Godot, toolchain REA/Ghidra para análisis de binarios, gotchas de entorno.
5. `docs/encyclopedia/README.md` — tabla de cobertura de la enciclopedia.
6. `docs/encyclopedia/formats/*.md` — las cuatro entradas de formato escritas hasta
   ahora (wad3, bsp30, mdl10, spr).
7. `tools/verify/*.py` — los validadores (la fuente de verdad de las mediciones).
8. `data/corpus/` y `data/validation/` — inventario y resultados (generados, nunca
   escritos a mano).
9. `src/parsers/goldsrc/` — los parsers del motor y sus structs pineados.

## 3. Qué es el proyecto (resumen de un párrafo)

Quebratsk Engine es una GDExtension C++23 para Godot 4 que monta una instalación retail
de GoldSrc/Source/Enfusion y convierte sus assets (BSP30, WAD3, MDL10, SPR, VPK, VTF,
PBO…) en recursos de Godot, más un editor dock y un lab jugable (`demo/lab/`) que
reimplementa comportamiento del motor GoldSrc (colisión, agua, escaleras, sonidos,
puertas, entidades). La capa parser/IR/converter está deliberadamente desacoplada del
motor: los parsers no tocan `Object`/`String`/`Variant`, lo que habilita tests headless.

## 4. La iniciativa bajo auditoría: GoldSrc Encyclopedia

Objetivo declarado: una enciclopedia de formatos y comportamiento GoldSrc donde **toda
afirmación está verificada contra el corpus retail** (22 carpetas de juego instaladas),
no recordada de memoria ni copiada de wikis. Regla de evidencia: cada hecho se clasifica
como `observed` (medido), `decompiled` (binario, Stage 2.5) u `hypothesis` (sin
confirmar), y nada `hypothesis` se publica como si fuera cierto.

Estado por stage:

| Stage | Qué cubre | Estado |
|---|---|---|
| 0 | Scanner de corpus + inventario determinista | **Hecho** |
| 1 | Validadores de formato contra todo el corpus (WAD3, BSP30, MDL10, SPR, PAL/LMP/DEM/RES/CFG) | **Parcial** — faltan PAL/LMP/DEM/RES/CFG |
| 2 | Semántica del motor desde el SDK (movimiento, límites, sonidos…) | Pendiente |
| 2.5 | Análisis de binarios del motor con REA/Ghidra | Pendiente |
| 3 | Observación dinámica del motor retail (demos, screenshots) | Pendiente |
| 4 | Atlas de contenido por juego y por mapa | Pendiente |
| 5 | Cerrar los gaps del lab (lightmaps, sprites, texturas animadas…) | Pendiente |
| 6 | Publicación de la enciclopedia | Pendiente |

**Gate de Stage 1 (estado actual):**

```
wad3     126 files    0 hard   1,137 warnings
bsp30    397 files    0 hard     431 warnings
mdl10  3,312 files    0 hard     101 warnings
spr    1,361 files    0 hard       0 warnings
pal/lmp/dem/res/cfg   not implemented
GATE PARTIAL: 1 format group still has no validator
```

## 5. Qué hicimos y cómo (metodología, para que la audites con contexto)

**Stage 0** (`tools/corpus_scan.py`): camina 22 carpetas de juego, registra path,
tamaño, SHA-256, formato y versión para `.bsp/.wad/.mdl/.spr/.wav/.tga/.pak/.res/.cfg/
.txt/.lst`, detecta duplicados por hash (7.460 archivos duplicados, 254 MB
redundantes), y genera `data/corpus/*.json` + `docs/encyclopedia/games/*.md` de forma
determinista (sin timestamps ni rutas absolutas; dos corridas producen los mismos
bytes).

**Stage 1** (`tools/verify/`): un validador Python por formato que recorre el corpus
completo, clasifica errores en **hard** (un lector debe rechazarlos) y **warnings**
(tolerados o convenciones que el corpus nunca rompe), y un gate
(`validate_corpus.py --all`) que reporta el estado honestamente: un formato sin
validador aparece como `not implemented`, nunca como aprobado. Los resultados quedan en
`data/validation/*.json`.

**Metodología de trabajo (así hicimos cada formato):**

1. Probar hipótesis de layout contra los bytes reales del corpus con scripts de
   sondeo, hasta que un walk cierre exacto contra el tamaño de archivo en TODOS los
   archivos. Nada se da por sabido: el header SPR resultó ser de 40 bytes, no los 36
   del `dsprite_t` de Quake que figura en todas las wikis.
2. Escribir el validador como fuente de verdad de las mediciones.
3. Pinear los mismos offsets en los structs C++ (`static_assert` de tamaño y
   `offsetof`), con el hallazgo documentado.
4. Escribir la entrada de formato en `docs/encyclopedia/formats/` citando SOLO hechos
   medidos, incluida una sección "lo que el corpus corrige".
5. Actualizar el gate y volver a correrlo.

**Hallazgos que ilustran por qué medimos en vez de recordar:**

- El header SPR es de 40 bytes con `tex_format` en el offset 12 (antes del radio); el
  palette count (256) está en el offset 40 en 1.361/1.361 archivos.
- Cada frame SPR lleva un `int32` de tipo adelante; NO existe RLE en ningún sprite
  retail (todos son `w*h` índices crudos); el radio es exactamente
  `hypot(width,height)/2` en 1.361/1.361; `palette[255]` no es un color key (su RGB es
  arbitrario: negro en 634, blanco en 417, azul en 65…).
- MDL10: los vértices de triángulos se cuentan de una forma que contradice la
  documentación habitual, y 204 "modelos" del corpus son en realidad contenedores
  puros (sin huesos ni secuencias).
- BSP30: dos layouts de lumps distintos entre juegos (`standard` vs `bshift`).

**Bug de motor encontrado midiendo y arreglado** (`11e0d74`): el parser SPR del engine
arrancaba en el frame sin saltar el int32 de tipo; con `origin_y` negativo (la mayoría
de los sprites retail) el width quedaba negativo y se decodificaban cero frames. El fix
consume el tipo, soporta grupos (tipo 1) y devuelve los frames parciales ante
truncamiento. Verificación en dos capas: test sintético con el caso de regresión (7
casos, corre bajo ctest: 7/7 suites en verde) y un barrido del corpus completo donde un
decode Python independiente produjo RGBA idéntico byte a byte (FNV-1a 64 sobre
metadata+píxeles) al parser C++ en 1.361/1.361 archivos (7.634 frames, 64.429.376
bytes de píxeles, 0 errores).

## 6. Qué falta (estado honesto)

1. **Grupo PAL/LMP/DEM/RES/CFG** (último de Stage 1). Medido en el corpus: `.pal` 0
   archivos, `.dem` 0, `.lmp` 7 (palettes/colormap/consola), `.res` 291, `.cfg` 108.
   Pregunta abierta de diseño: ¿cómo se valida un formato del que el corpus no tiene
   muestras sin violar la regla de "toda afirmación sale del corpus"?
2. **Stages 2–6** completos (ver tabla de §4).
3. **Deudas previas del audit 2026-11-11** que siguen abiertas, entre otras: las suites
   GDScript salen 0 sin verificar nada; lightmaps (lump 8) sin leer; el último release
   no contiene el trabajo de `main`; CI pineado a Godot 4.3 mientras el proyecto
   apunta a 4.7.

## 7. Comandos para verificar por tu cuenta (no confíes en este brief)

```powershell
# Gate de Stage 1 (debe reportar GATE PARTIAL, 0 hard errors)
python tools/verify/validate_corpus.py --all --json data/validation/corpus.json

# Un validador individual
python tools/verify/validate_spr.py --json data/validation/spr.json

# Regenerar el inventario (determinista; debe producir los mismos bytes)
python tools/corpus_scan.py scan

# Tests C++ (7 suites, ctest). Requiere entorno MSVC:
# desde el Developer Command Prompt (vcvars64) o anteponiendo la llamada a vcvars64.bat
cmake --build build-ninja --target spr_parser_test
ctest --test-dir build-ninja --output-on-failure
```

**Entorno:** Windows 11, VS 2022 Community (MSVC 14.44), CMake 3.31 (bundled con VS,
en `Common7\IDE\CommonExtensions\Microsoft\CMake`), Ninja, Python 3.14.7. El corpus
retail está en `C:\Steam}\steamapps\common\Half-Life` (sí: la carpeta se llama `Steam}`
con llave — es la prueba de regresión contra hardcodear rutas; el código resuelve la
raíz vía `QUEBRATSK_GAME_ROOT` o el registro de Steam). Godot 4.7.2 está instalado.
**A diferencia de la auditoría previa, esta máquina SÍ tiene Steam y el corpus
completo: podés correr los validadores contra los archivos reales.**

## 8. Preguntas que queremos que contestes con evidencia

### A. Metodología de la iniciativa

1. ¿El enfoque "corpus-first" (medir 22 carpetas de juego antes de escribir un
   validador) es el correcto para especificar formatos binarios? ¿Hay mejores formas
   probadas? ¿Qué le falta para ser riguroso de verdad?
2. El conocimiento del layout vive en DOS lugares: validadores Python y structs C++
   pineados con static_asserts. ¿Es aceptable esa duplicación o existe un patrón mejor
   (single source of truth, codegen, schema compartido)?
3. ¿La clasificación hard/warning es sólida? ¿Los umbrales (p. ej. dims 1..4096,
   grupos máx 64) están bien elegidos o son arbitrarios?
4. ¿La regla de evidencia (`observed` / `decompiled` / `hypothesis`) se aplica de forma
   consistente en los cuatro formatos publicados? Buscá afirmaciones en
   `docs/encyclopedia/formats/` que no estén respaldadas por el validador correspondiente.

### B. Calidad del código (validadores y parsers)

5. Los validadores Python: ¿estructura, mantenibilidad, performance sobre ~1,3 GB de
   corpus? ¿Errores de diseño que se te hagan evidentes?
6. Los structs C++ con `#pragma pack(1)` + `reinterpret_cast` sobre bytes de archivo:
   ¿es el patrón correcto para este proyecto o debería migrarse a lectura explícita de
   offsets (estilo `ByteReader` del repo)? ¿Qué riesgos de UB quedan?
7. El parser SPR arreglado (`src/parsers/goldsrc/spr_parser.cpp`): ¿es correcto y
   robusto ante entrada hostil? ¿La tolerancia a truncamiento (devolver frames
   parciales) es la decisión correcta para este motor o debería fallar todo el archivo?
8. ¿`std::expected` como modelo de error escala para los demás parsers? ¿Qué patrón
   alternativo considerarías y por qué?

### C. El gate

9. ¿El diseño del gate es honesto y útil? ¿Qué le falta para ser parte de CI (p. ej.
   correr en cada PR sin depender de Steam)?
10. PAL y DEM tienen CERO archivos en el corpus. ¿Qué hacemos con ellos: validador
    "de guardia" con el layout documentado como no verificado, sacarlos del grupo, o
    buscar muestras por otro lado? ¿Qué haría un proyecto serio aquí?

### D. Testing

11. ¿La estrategia "test sintético + barrido full-corpus contra un decode
    independiente" es suficiente? ¿Qué agregarías (property-based, fuzzing de los
    parsers, golden files, round-trip) y qué es lo que da más valor por costo?
12. Las 21 suites GDScript salen 0 sin verificar nada (hallazgo del audit previo, sigue
    vigente). ¿Cómo lo arreglamos sin reescribir todo?

### E. Documentación

13. ¿El formato de las entradas de `docs/encyclopedia/formats/` es bueno para el
    objetivo declarado ("alguien que nunca vio el repo escribe un lector BSP30 desde
    los docs")? ¿Qué le falta?
14. ¿La separación "nada bajo `data/` ni `docs/encyclopedia/games/` se escribe a mano"
    está bien implementada? ¿Hay un mecanismo de drift-check que valga la pena agregar?

### F. El plan completo (Stages 0–6)

15. ¿El orden de stages es el correcto? ¿Vamos por buen camino o hay una bifurcación
    más valiosa antes de Stage 2? ¿Algún stage es riesgoso por depender de un supuesto
    no verificado?
16. ¿Qué nos falta en el plan? (cosas como round-trip validation, conformance suite
    contra el motor retail, licensing de los datos derivados, mantenibilidad del
    corpus a futuro).

### G. El proyecto entero (línea de base del audit previo)

17. Reauditá las deudas de `AUDIT-2026-11-11.md` y decinos cuáles persisten, cuáles
    cambiaron y cuál es la prioridad de cada una HOY (lightmaps, tests GDScript,
    release hygiene, CI, documentación, licensing de los skyboxes de Valve).

## 9. Reglas del encargo

- **Evidencia > opinión.** Verificá con comandos y archivos; citá archivo y línea
  cuando afirmes algo. Lo que no puedas verificar en esta máquina, marcalo como "no
  verificado" (como hizo correctamente el audit previo con los números de runtime).
- **Read-only.** No modifiques el working tree. Si necesitás generar archivos
  temporales, usá el directorio temporal del sistema.
- **Idioma:** podés responder en español o en inglés; el que prefieras. Los archivos
  del repo están en inglés salvo los meta-docs de raíz (KICKOFF y este brief).
- **Recomendaciones fundadas.** Para cada idea de mejora: qué problema concreto
  resuelve, cuál es la práctica/técnica probada detrás, y qué costaría adoptarla acá.
  Si no estás seguro de que una práctica aplica, decilo — preferimos "esto es una
  conjetura" a una recomendación vaga presentada como certeza.

## 10. Entregables (checklist para tu respuesta)

- [ ] Veredicto por área, tabla con grados y una línea de justificación (actualizado a `2e578bb`).
- [ ] Lista de qué hacemos bien, con evidencia.
- [ ] Lista de qué hacemos mal, con evidencia y explicación de por qué.
- [ ] Ideas de mejora concretas y fundamentadas, en cualquier aspecto del proyecto.
- [ ] Plan priorizado de siguientes pasos (hacer ya / hacer después / no hacer).
- [ ] Comparación explícita con `AUDIT-2026-11-11.md`: qué cambió y qué sigue igual.
- [ ] Respuesta a las 17 preguntas de §8 (o indicación explícita de cuáles dejás sin
      responder y por qué).
