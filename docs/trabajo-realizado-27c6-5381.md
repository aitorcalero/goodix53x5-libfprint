# Trabajo realizado para habilitar el lector Goodix 27c6:5381

> Informe histórico de las primeras pruebas. Las rutas, el hash de biblioteca y
> la instalación descritos corresponden a aquella sesión, no al estado actual.
> Después aparecieron fallos intermitentes de reconocimiento, incluso tras
> reiniciar el servicio. La interpretación de la imagen descrita aquí sigue
> siendo una hipótesis del prototipo. Consulta las
> [notas actualizadas](27c6-5381-porting.md) y la
> [evaluación para contribuir a Milan](5381-milan-contribution.md).

## Resultado actual

El lector de huellas del Dell G5 15 5587 ya es reconocido por `fprintd` como:

```text
Goodix HTK32 Fingerprint Sensor
```

El sistema tiene registrado el índice derecho del usuario `aitor`. El comando
`fprintd-list aitor` encuentra un dispositivo y muestra
`right-index-finger` como huella registrada.

El soporte sigue marcado como experimental porque solo se ha probado en una
unidad física y con un dedo. La colocación del dedo influye bastante en el
resultado.

## 1. Diagnóstico inicial

El mensaje original era:

```text
Impossible to enroll:
GDBus.Error:net.reactivated.Fprint.Error.NoSuchDevice:
No devices available
```

Se comprobó que el lector USB es un Goodix con identificador `27c6:5381`. La
versión normal de `libfprint` instalada en Arch Linux no incluía un controlador
compatible, por lo que `fprintd` no podía enumerarlo.

Datos confirmados en este equipo:

- Equipo: Dell G5 15 5587.
- USB VID:PID: `27c6:5381`.
- Interfaz USB de datos: `1`.
- Endpoint de salida: `0x03`.
- Endpoint de entrada: `0x81`.
- Tamaño máximo de paquete: 64 bytes.
- Chip ID: `0x002202a0`.
- OTP: 32 bytes.
- Firmware observado: `GF5288_HTSEC_APP_10020` y
  `GF3208_HTSEC_APP_10020`.

## 2. Investigación del protocolo

Se estudió el proyecto
[`goodix53x5-libfprint`](https://github.com/AndyHazz/goodix53x5-libfprint), el
controlador oficial de Dell para PID 5381 y el trabajo de referencia
[`goodix-27c6-55a4-fingerprint-linux`](https://github.com/Hydrogell/goodix-27c6-55a4-fingerprint-linux).

También se abrió el
[`issue #29`](https://github.com/AndyHazz/goodix53x5-libfprint/issues/29) para
documentar y proponer la incorporación del dispositivo al proyecto.

El análisis mostró que este sensor usa la variante **Milan F**, diferente de la
variante utilizada por otros lectores 53x5. Para el PID 5381 hacen falta:

- La entrada 0 de la tabla de configuración del controlador de Dell.
- Una orden de preparación de imagen de dos bytes: `01 00`.
- Cálculos de calibración específicos a partir de la OTP.
- Aceptar las dos cadenas de firmware observadas en el mismo dispositivo.
- Procesar de forma especial la distribución espacial de la imagen.

## 3. Cambio de la PSK

El sensor utiliza una PSK persistente para establecer la sesión cifrada GTLS.
Con autorización expresa, la PSK propia del equipo se sustituyó por la PSK
conocida formada por ceros que usa este controlador experimental.

Después de escribirla se volvió a leer su hash y se comprobó que coincidía
antes de iniciar GTLS. El cambio permanece guardado en el lector y puede afectar
a la compatibilidad posterior con el controlador de Windows.

No se escribió ni modificó el firmware del lector.

El código mantiene dos protecciones independientes:

- `GOODIX53X5_ALLOW_FULL_INIT=1` permite inicializar completamente el 5381.
- `GOODIX53X5_ALLOW_PSK_WRITE=1` permite escribir la PSK cuando sea necesario.

La segunda variable no está configurada en el servicio actual, porque la PSK ya
se cambió y no debe volver a escribirse en cada arranque.

## 4. Código desarrollado

El trabajo se realizó en la rama Git `experimental-27c6-5381` del repositorio:

```text
/home/aitor/goodix53x5-libfprint
```

Los tres commits principales son:

```text
be924b8 Add guarded experimental support for Goodix 5381
1fd1a0b Use Milan F protocol for Goodix 5381
5566213 Complete image processing for Goodix 5381
```

### Detección e inicialización

Se añadió `27c6:5381` a la tabla de dispositivos y se implementaron
comprobaciones para el chip ID, la longitud de la OTP y las dos versiones de
firmware conocidas. El controlador puede hacer ping, reset, consulta de
firmware, consulta del chip, lectura de OTP, sesión GTLS, carga de configuración
y calibración FDT.

La inicialización completa permanece protegida por una variable de entorno para
evitar cambios accidentales en unidades no verificadas.

### Captura y reconstrucción de imagen

Cada captura descifrada contiene exactamente 14.256 bytes después de retirar
los cuatro bytes del CRC. Son 9.504 muestras de 12 bits, equivalentes a una
imagen de 108 × 88 píxeles.

Las muestras no llegan en orden lineal. Cada grupo de cuatro valores representa
un bloque de 2 × 2 píxeles. Para `q0`, `q1`, `q2` y `q3`, la colocación correcta
es:

```text
q0 q3
q1 q2
```

Se añadió una función que reconstruye esa distribución. Después de hacerlo se
descubrió que la imagen contiene dos lecturas contiguas de 54 × 88 del mismo
área física. Ambas mitades se promedian y el resultado se amplía horizontalmente
a 108 × 88 para mantener el tamaño esperado por SIGFM.

Este tratamiento evita descriptores SIFT duplicados que antes confundían la
comparación geométrica.

### Referencia sin dedo

El sensor puede informar de que el dedo se ha retirado antes de que haya salido
físicamente del área de lectura. Si se tomaba una nueva referencia tras cada
etapa, parte de la huella acababa incluida en la referencia siguiente.

Durante el enrolamiento ahora se captura una sola referencia sin dedo al inicio
y se conserva durante las ocho muestras.

### Correcciones en SIGFM

Se corrigieron dos problemas numéricos del comparador:

- Los cocientes usados por `asin` y `acos` se limitan al intervalo `[-1, 1]`.
- Los ángulos relativos se comparan mediante una tolerancia absoluta de 0,10
  radianes.

Estas correcciones siguen las aplicadas en el proyecto LGPL de referencia para
el Goodix 55a4. La versión interna del formato de plantilla SIGFM se elevó a 3
para impedir que plantillas creadas con un procesamiento anterior se mezclen
con las nuevas.

## 5. Pruebas realizadas

Las capturas cifradas superaron estas comprobaciones:

- HMAC del contenido cifrado.
- CRC de la imagen GEA.
- Descifrado completo de la imagen.
- Reconstrucción y normalización de la captura.
- Extracción de características SIGFM/SIFT.

El enrolamiento experimental completó sus ocho etapas. Las muestras contenían
entre 173 y 228 puntos característicos. Las 28 comparaciones posibles entre las
ocho muestras superaron el umbral 150; la puntuación mínima fue 240.

En una verificación independiente se obtuvo `MATCH` con una puntuación de 778.
Una lectura anterior obtuvo 40 y no coincidió, lo que confirma la sensibilidad
a la posición del dedo.

También se ejecutó `scripts/run-tests.sh`. Pasaron las pruebas de criptografía,
extracción SIGFM y comparación SIGFM. La biblioteca completa se compiló
correctamente. Las pruebas normales de Meson pasaron salvo el validador de
metadatos, que no pudo consultar `fprint.freedesktop.org` por falta de acceso
DNS durante la ejecución; no era un fallo del controlador.

## 6. Instalación activa en el sistema

La biblioteca experimental se instaló de forma aislada en:

```text
/opt/libfprint-goodix53x5/lib/libfprint-2.so.2.0.0
```

SHA-256 de la biblioteca instalada:

```text
2f5f91a01637cd4e1048f814dca728580f588a9d35ae09777b217b67ed2862db
```

No se reemplazó `/usr/lib/libfprint-2.so.2`, que continúa perteneciendo al
paquete de Arch. Solo `fprintd` carga la biblioteca experimental mediante este
archivo de systemd:

```text
/etc/systemd/system/fprintd.service.d/goodix53x5.conf
```

Su contenido es:

```ini
[Service]
Environment=LD_LIBRARY_PATH=/opt/libfprint-goodix53x5/lib
Environment=GOODIX53X5_ALLOW_FULL_INIT=1
```

Después se ejecutaron `systemctl daemon-reload` y
`systemctl restart fprintd.service`. La configuración persiste tras reiniciar el
equipo.

El enrolamiento real mediante `fprintd-enroll` completó ocho etapas y terminó
con `enroll-completed`. La huella quedó en el almacén administrado por
`fprintd`, bajo `/var/lib/fprint`.

## 7. Cómo comprobarlo

Mostrar el lector y las huellas registradas:

```bash
sudo fprintd-list aitor
```

Probar el índice derecho:

```bash
fprintd-verify -f right-index-finger "$USER"
```

Tras `Verify started!`, hay que colocar el índice derecho centrado y cubrir la
mayor parte posible del sensor. Una coincidencia correcta aparece como:

```text
Verify result: verify-match (done)
```

Consultar el estado del servicio:

```bash
sudo systemctl status fprintd
```

## 8. Cómo revertir la instalación experimental

Estos comandos eliminan el override y la biblioteca aislada, y hacen que
`fprintd` vuelva a utilizar la versión de `libfprint` instalada por Arch:

```bash
sudo rm /etc/systemd/system/fprintd.service.d/goodix53x5.conf
sudo rm -rf /opt/libfprint-goodix53x5
sudo systemctl daemon-reload
sudo systemctl restart fprintd
```

La PSK escrita en el lector es persistente y estos comandos no restauran la PSK
original.

Para borrar la huella registrada del usuario:

```bash
sudo fprintd-delete aitor
```

## 9. Privacidad de los datos de prueba

Las imágenes y plantillas temporales utilizadas para estudiar el formato se
eliminaron. `test-storage.variant` se añadió a `.gitignore` para impedir su
inclusión accidental. No se guardaron capturas biométricas ni plantillas en los
commits del repositorio.
