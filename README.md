# SilentPixel

SilentPixel es un visor local de imÃ¡genes para Windows con inspecciÃ³n tÃ©cnica de metadatos.

EstÃ¡ diseÃ±ado para abrir imÃ¡genes desde el propio equipo, mostrar informaciÃ³n Ãºtil del archivo y evitar dependencias innecesarias de servicios externos.

![Vista previa de SilentPixel](assets/silentpixel-preview-mclaren.png)

## Objetivo

SilentPixel nace como una alternativa ligera, portable y privada a los visores de imÃ¡genes modernos que dependen de cuentas, servicios en segundo plano, sincronizaciÃ³n, nube o telemetrÃ­a.

La idea es sencilla:

- abrir imÃ¡genes locales;
- mostrar los metadatos que el archivo ya contiene;
- no llamar a internet;
- no abrir mapas;
- no crear historial de archivos recientes;
- no depender de servicios externos.

## Funciones principales

- Apertura de imÃ¡genes JPG, JPEG, PNG, BMP, HEIC y HEIF.
- NavegaciÃ³n entre imÃ¡genes de una carpeta.
- Zoom, ajuste a ventana, tamaÃ±o real y pantalla completa.
- RotaciÃ³n visual.
- Panel de metadatos tÃ©cnico.
- ExportaciÃ³n de informe TXT.
- Copia de resumen, hash, huella visual y coordenadas.
- Hashes SHA-256, SHA-1 y MD5.
- Huellas visuales aHash/dHash para comparaciÃ³n aproximada.
- Lectura de EXIF, XMP, GPS, ICC/IPTC detectado y metadatos WIC cuando estÃ¡n disponibles.

## Privacidad

SilentPixel estÃ¡ pensado para funcionar de forma local.

- No usa nube.
- No usa cuentas.
- No incluye telemetrÃ­a propia.
- No abre mapas.
- No llama a APIs externas.
- No guarda historial/MRU.
- No instala servicios en segundo plano.
- No usa Electron ni WebView.
- No abre enlaces encontrados dentro de metadatos.

Si una imagen contiene coordenadas GPS, SilentPixel las muestra como texto y permite copiarlas. La aplicaciÃ³n no abre Google Maps, no consulta servicios de mapas y no envÃ­a esas coordenadas a terceros.

## Metadatos

SilentPixel muestra informaciÃ³n que ya estÃ¡ dentro del archivo, cuando existe:

- cÃ¡mara o dispositivo;
- modelo;
- software;
- fecha original;
- ISO;
- apertura;
- velocidad de obturaciÃ³n;
- distancia focal;
- lente;
- orientaciÃ³n EXIF;
- coordenadas GPS;
- bloques EXIF/XMP/ICC/IPTC;
- identificadores XMP;
- software de ediciÃ³n;
- sistema o plataforma indicada por el propio archivo.

SilentPixel no inventa metadatos ni intenta deducir informaciÃ³n que el archivo no expone.

> "La foto ya venÃ­a hablando." SilentPixel solo le pone subtÃ­tulos.

## Contenedor tÃ©cnico

El apartado de contenedor funciona como inventario tÃ©cnico del JPEG/HEIC:

- EXIF detectado;
- XMP detectado;
- ICC detectado;
- IPTC detectado;
- tamaÃ±o de bloques;
- vista previa de XMP cuando es texto Ãºtil.

ICC e IPTC se muestran como bloques detectados, sin intentar convertir el informe en una salida completa de ExifTool.

## Formatos soportados

SilentPixel abre:

- JPG / JPEG
- PNG
- BMP
- HEIC / HEIF

El paquete portable de Windows incluye las librerÃ­as necesarias para abrir HEIC/HEIF. No hace falta instalar extensiones de Microsoft Store ni aÃ±adir componentes externos para usar HEIC desde la versiÃ³n portable.

Para HEIC/HEIF, conserva todos los archivos incluidos en la carpeta portable. No ejecutes `SilentPixel.exe` separado de sus DLL.

## RAW

SilentPixel no abre RAW en esta versiÃ³n.

Formatos como NEF, CR2, CR3, ARW, RAF, ORF, RW2, DNG, PEF o SRW son archivos de trabajo/revelado, no formatos finales de visualizaciÃ³n general.

## Dispositivos probados

SilentPixel se ha probado con archivos reales de distintas cÃ¡maras y dispositivos, incluyendo:

### Apple

- iPhone 11
- iPhone 12 Pro Max
- iPhone 14 Pro Max
- iPhone 16 Pro Max
- iPhone 17 Pro Max
- iPhone XS Max

### Android

- Xiaomi REDMI Note 15
- Xiaomi Redmi Note 9 Pro
- Xiaomi Redmi 4A
- Samsung Galaxy S23 Ultra
- Google Pixel 7 Pro
- Motorola moto g85 5G

### Canon

- Canon EOS 5D
- Canon EOS R
- Canon EOS R6
- Canon EOS R7

### Nikon

- Nikon D3100
- Nikon D40X
- Nikon COOLPIX S3600
- Nikon Z 6_2
- Nikon Z 7

### Sony

- Sony ILCE-1M2 / A1 II
- Sony ILCE-9M3 / A9 III
- Sony ILCE-7RM5 / A7R V
- Sony ILCE-6700 / A6700
- Sony ILCE-7CM2 / A7C II
- Sony ILCE-7M4 / A7 IV

### Fujifilm

- Fujifilm X-T2

### Panasonic

- Panasonic DMC-TZ7
- Panasonic DMC-FZ30

### OM System / Olympus

- OM Digital Solutions OM-1
- OM Digital Solutions OM-5

### Pentax

- Pentax K-1

### Leica

- Leica Q Typ 116
- Leica Q2

### Ricoh

- Ricoh GR III

### GoPro

- GoPro HERO 7
- GoPro HERO9 Black
- GoPro HERO10 Black
- GoPro Max

### DJI

- DJI FC220

## InstalaciÃ³n

SilentPixel es portable.

1. Descarga el ZIP de la release.
2. Extrae la carpeta completa.
3. Ejecuta `SilentPixel.exe`.

No requiere instalaciÃ³n tradicional.

Para que HEIC/HEIF funcione correctamente, mantÃ©n `SilentPixel.exe` junto al resto de archivos incluidos en el paquete portable.

## Uso bÃ¡sico

- Abrir: selecciona una imagen.
- Anterior / Siguiente: navega por la carpeta actual.
- Ajustar: ajusta la imagen a la ventana.
- 1:1: muestra tamaÃ±o real.
- Rotar: rota visualmente la imagen.
- Pantalla: alterna pantalla completa.
- Metadatos: muestra u oculta el panel tÃ©cnico.
- Exportar TXT: genera un informe tÃ©cnico del archivo actual.

## Licencia

MIT License.

Consulta el archivo `LICENSE`.

## Estado

VersiÃ³n inicial pÃºblica: `v0.1.0`.

SilentPixel estÃ¡ en fase inicial, pero ya es funcional como visor local/offline e inspector tÃ©cnico de metadatos para imÃ¡genes comunes.

