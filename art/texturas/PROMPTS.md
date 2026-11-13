# Texturas para generar en Higgsfield

## Qué elegir en la página

- **Modelo:** `GPT Image 2.6 Sunburst` (el que dice ILIMITADO). Es el mismo con el que hice la
  prueba del pasto y salió sin costura. Si alguna no sale bien, probá la misma con
  `Nano Banana Pro` (también ilimitado).
- **Interruptor "Ilimitado":** prendido.
- **Formato:** `1:1` (cuadrado), salvo las tres de árboles, que van en `2:3` (vertical).
- **Resolución:** `2K`. **Calidad:** la más alta que deje sin gastar créditos (Medio o Alto).
- **Cantidad:** 2 por prompt, así elegís la mejor.

## Cómo pasármelas

Guardalas en esta misma carpeta (`art/texturas/`) con el nombre que figura arriba de cada
prompt (por ejemplo `pasto.png`). Si de alguna te gustan dos, guardá `pasto_1.png` y
`pasto_2.png`. No importa si baja como `.png`, `.jpg` o `.webp`.

## Qué mirar para elegir

- Que sea **pareja en toda la imagen**: sin una zona más clara u oscura, sin un objeto que
  llame la atención (una piedra grande, una flor, una mancha). Eso al repetirse se nota mucho.
- Que **no tenga sombras marcadas** ni se vea en perspectiva.
- Las de árboles: fondo magenta liso, árbol entero, sin cortar la copa ni el tronco.

Pegá cada prompt tal cual, en inglés.

---

## A. Suelo del fondo lejano (lo más importante, van primero)

### `pasto.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of a mown green grass meadow with a few subtle patches of clover and slightly drier blades, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no perspective, no horizon, no objects, no flowers, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `pasto_seco.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of dry yellowed pasture grass mixed with patches of bare pale earth, late summer, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no perspective, no horizon, no objects, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `bosque_desde_arriba.png`
```
Seamless tileable texture, aerial photograph looking straight down at a dense temperate forest canopy from 300 meters altitude, hundreds of small treetops packed together, mixed broadleaf and conifer, natural muted greens, uniform density across the whole frame, flat even overcast lighting, no clearings, no roads, no buildings, no water, no perspective, no vignette, photorealistic, edges match so it repeats without a seam
```

### `bosque_pinos_desde_arriba.png`
```
Seamless tileable texture, aerial photograph looking straight down at a dense dark conifer pine forest canopy from 300 meters altitude, hundreds of small pointed treetops packed together, dark blue-green, uniform density across the whole frame, flat even overcast lighting, no clearings, no roads, no buildings, no snow, no perspective, no vignette, photorealistic, edges match so it repeats without a seam
```

### `roca.png`
```
Seamless tileable game texture, flat frontal orthographic view of a weathered grey granite cliff face with fine cracks, small ledges and sparse lichen, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no perspective, no sky, no plants, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `piedras_sueltas.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of alpine scree, small broken grey rocks and gravel with a little dry dirt between them, stones all of similar size, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no perspective, no plants, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `nieve.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of wind-packed mountain snow with soft shallow ripples and a faint grainy crust, very pale blue-white, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no footprints, no rocks, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `arena.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of desert sand with fine regular wind ripples, warm pale ochre, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no footprints, no stones, no plants, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `desierto_pedregoso.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of arid desert ground, cracked hard reddish-brown earth scattered with small pebbles and a few tiny dry twigs, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no plants, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `tierra.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of bare compacted brown dirt with small stones and a few dry leaves and roots, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no grass, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

---

## B. Ciudad lejana (fachadas y techos)

Estas tienen que salir **de frente y bien derechas**, como un plano. Si las ventanas salen
torcidas o en perspectiva, descartala.

### `fachada_monoblock.png`
```
Seamless tileable texture, perfectly flat frontal orthographic elevation of a 1970s concrete apartment block facade, regular grid of identical rectangular windows, four windows wide and four floors tall, stained grey precast concrete panels, some windows with curtains, no balconies, no ground floor, no roof, no sky, no street, no people, no perspective, straight vertical and horizontal lines, flat even overcast lighting, no cast shadows, photorealistic, edges match so it repeats without a seam in both directions
```

### `fachada_ladrillo.png`
```
Seamless tileable texture, perfectly flat frontal orthographic elevation of an old red brick industrial building facade, regular grid of identical tall windows with dark metal frames, four windows wide and three floors tall, weathered brick with soot stains, no doors, no ground floor, no roof, no sky, no street, no people, no perspective, straight vertical and horizontal lines, flat even overcast lighting, no cast shadows, photorealistic, edges match so it repeats without a seam in both directions
```

### `fachada_oficinas.png`
```
Seamless tileable texture, perfectly flat frontal orthographic elevation of a modern office tower curtain wall, regular grid of identical blue-grey glass panels with thin aluminium mullions, six panels wide and six floors tall, dull muted reflections of an overcast sky, no ground floor, no roof, no sky, no street, no people, no perspective, straight vertical and horizontal lines, flat even lighting, photorealistic, edges match so it repeats without a seam in both directions
```

### `fachada_revoque.png`
```
Seamless tileable texture, perfectly flat frontal orthographic elevation of a worn plastered residential building facade, faded beige paint with water stains, regular grid of identical small windows with closed roller shutters, four windows wide and four floors tall, a few air conditioner units, no balconies, no ground floor, no roof, no sky, no street, no people, no perspective, straight vertical and horizontal lines, flat even overcast lighting, no cast shadows, photorealistic, edges match so it repeats without a seam in both directions
```

### `techo.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of a flat building roof covered in old grey bitumen membrane with fine gravel, faint seams and a few dark water stains, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no objects, no vents, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `asfalto.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of worn dark grey asphalt road surface with fine aggregate and a few hairline cracks, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no road markings, no manholes, no puddles, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

---

## C. Árboles sueltos (formato 2:3 vertical)

Son para recortar y plantar cerca del nivel. El fondo magenta es a propósito: lo saco yo.

### `arbol_hoja.png`
```
A single complete deciduous oak tree in full summer leaf, photographed straight from the side at eye level, whole tree visible from the base of the trunk to the top of the crown with margin around it, centered, isolated on a perfectly flat solid pure magenta background color #FF00FF, flat even overcast lighting, no ground, no grass, no cast shadow, no other objects, photorealistic
```

### `arbol_pino.png`
```
A single complete tall pine tree, photographed straight from the side at eye level, whole tree visible from the base of the trunk to the pointed top with margin around it, centered, isolated on a perfectly flat solid pure magenta background color #FF00FF, flat even overcast lighting, no ground, no grass, no cast shadow, no other objects, photorealistic
```

### `arbol_seco.png`
```
A single complete scrubby dry desert tree with sparse small leaves and a twisted trunk, photographed straight from the side at eye level, whole tree visible from the base of the trunk to the top with margin around it, centered, isolated on a perfectly flat solid pure magenta background color #FF00FF, flat even overcast lighting, no ground, no cast shadow, no other objects, photorealistic
```

---

## D. Materiales para tus propios mapas (opcional, cuando quieras)

Son genéricos. Cuando me digas de qué va tu juego (urbano, industrial, campo, desierto) te
armo una lista a medida.

### `hormigon_pared.png`
```
Seamless tileable game texture, flat frontal orthographic view of a bare poured concrete wall with faint formwork lines, small air holes and light water staining, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no graffiti, no cracks larger than a hairline, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `hormigon_piso.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of a worn smooth concrete floor with fine dust, faint scuff marks and tiny chips, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no joints, no objects, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `ladrillo.png`
```
Seamless tileable game texture, flat frontal orthographic view of an old red brick wall in running bond with weathered grey mortar, bricks slightly varied in tone, uniform detail across the whole frame, perfectly horizontal courses, flat even overcast lighting, no cast shadows, no graffiti, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `revoque_gastado.png`
```
Seamless tileable game texture, flat frontal orthographic view of an old plastered interior wall with faded off-white paint, fine cracks, a few small peeled patches and light grime, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no objects, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `metal_oxidado.png`
```
Seamless tileable game texture, flat frontal orthographic view of a rusty painted steel plate, flaking grey-green paint over orange-brown rust, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no rivets, no seams, no text, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `chapa_acanalada.png`
```
Seamless tileable game texture, flat frontal orthographic view of weathered corrugated galvanized metal sheeting with vertical ridges, dull grey with light rust streaks running downward, ridges perfectly straight and evenly spaced, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no screws, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `madera_tablas.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of old worn wooden floor planks running vertically, grey-brown weathered wood with visible grain and thin dark gaps between boards, boards of equal width, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no nails, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```

### `baldosas.png`
```
Seamless tileable game texture, perfectly top-down orthographic view of old small square ceramic floor tiles in a regular grid, dull off-white with dirty grey grout and light wear, grid lines perfectly straight, uniform detail across the whole frame, flat even overcast lighting, no cast shadows, no reflections, no perspective, no vignette, photorealistic photo scan, edges match so it repeats without a seam
```
