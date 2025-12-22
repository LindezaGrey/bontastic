(function () {
    const TARGET_WIDTH = 384;
    const BAYER_4 = [
        [0, 8, 2, 10],
        [12, 4, 14, 6],
        [3, 11, 1, 9],
        [15, 7, 13, 5],
    ];

    async function loadImageBitmap(file) {
        if (window.createImageBitmap) {
            return await createImageBitmap(file);
        }
        const url = URL.createObjectURL(file);
        try {
            const img = new Image();
            img.src = url;
            await new Promise((resolve, reject) => {
                img.onload = resolve;
                img.onerror = reject;
            });
            const canvas = document.createElement('canvas');
            canvas.width = img.naturalWidth || img.width;
            canvas.height = img.naturalHeight || img.height;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(img, 0, 0);
            return await createImageBitmap(canvas);
        } finally {
            URL.revokeObjectURL(url);
        }
    }

    async function encodeBitmapFromFile(file, opts) {
        const dither = !!(opts && opts.dither);
        const bmp = await loadImageBitmap(file);

        const scale = TARGET_WIDTH / bmp.width;
        const height = Math.max(1, Math.round(bmp.height * scale));

        const canvas = document.createElement('canvas');
        canvas.width = TARGET_WIDTH;
        canvas.height = height;
        const ctx = canvas.getContext('2d', { willReadFrequently: true });
        ctx.imageSmoothingEnabled = true;
        ctx.drawImage(bmp, 0, 0, TARGET_WIDTH, height);

        const { data } = ctx.getImageData(0, 0, TARGET_WIDTH, height);

        const rowBytes = TARGET_WIDTH >> 3;
        const out = new Uint8Array(rowBytes * height);

        for (let y = 0; y < height; y++) {
            for (let xb = 0; xb < rowBytes; xb++) {
                let byte = 0;
                for (let bit = 0; bit < 8; bit++) {
                    const x = (xb << 3) + bit;
                    const i = (y * TARGET_WIDTH + x) << 2;
                    const r = data[i];
                    const g = data[i + 1];
                    const b = data[i + 2];
                    const gray = (r * 0.299 + g * 0.587 + b * 0.114);
                    const t = dither ? ((BAYER_4[y & 3][x & 3] + 0.5) / 16) * 255 : 128;
                    const black = gray < t;
                    if (black) {
                        byte |= 1 << (7 - bit);
                    }
                }
                out[y * rowBytes + xb] = byte;
            }
        }

        return { width: TARGET_WIDTH, height, bytes: out };
    }

    function drawEncodedToCanvas(encoded, canvas) {
        if (!canvas) {
            return;
        }
        canvas.width = encoded.width;
        canvas.height = encoded.height;
        const ctx = canvas.getContext('2d', { willReadFrequently: true });
        const img = ctx.createImageData(encoded.width, encoded.height);
        const data = img.data;
        const rowBytes = encoded.width >> 3;

        for (let y = 0; y < encoded.height; y++) {
            for (let xb = 0; xb < rowBytes; xb++) {
                const b = encoded.bytes[y * rowBytes + xb];
                for (let bit = 0; bit < 8; bit++) {
                    const x = (xb << 3) + bit;
                    const black = (b & (1 << (7 - bit))) !== 0;
                    const i = (y * encoded.width + x) << 2;
                    const v = black ? 0 : 255;
                    data[i] = v;
                    data[i + 1] = v;
                    data[i + 2] = v;
                    data[i + 3] = 255;
                }
            }
        }

        ctx.putImageData(img, 0, 0);
    }

    function hexPreview(bytes, maxBytes) {
        const take = Math.min(bytes.length, maxBytes || 128);
        let out = '';
        for (let i = 0; i < take; i++) {
            const h = bytes[i].toString(16).padStart(2, '0');
            out += h;
            out += (i + 1) % 16 === 0 ? '\n' : ' ';
        }
        if (take < bytes.length) {
            out += `\n… (+${bytes.length - take} bytes)`;
        }
        return out.trim();
    }

    async function writeInChunks(characteristic, bytes, chunkSize) {
        const canWriteWithoutResponse = !!(characteristic && characteristic.properties && characteristic.properties.writeWithoutResponse);
        const write = (canWriteWithoutResponse && characteristic.writeValueWithoutResponse)
            ? characteristic.writeValueWithoutResponse.bind(characteristic)
            : characteristic.writeValue.bind(characteristic);

        for (let i = 0; i < bytes.length; i += chunkSize) {
            await write(bytes.slice(i, i + chunkSize));
        }
    }

    async function sendBitmap(characteristic, encoded, chunkSize) {
        const expected = encoded.bytes.length;
        const header = new Uint8Array(8);
        header[0] = 0x42;
        header[1] = 0x4d;
        header[2] = encoded.height & 0xff;
        header[3] = (encoded.height >> 8) & 0xff;
        header[4] = expected & 0xff;
        header[5] = (expected >> 8) & 0xff;
        header[6] = (expected >> 16) & 0xff;
        header[7] = (expected >> 24) & 0xff;

        const packet = new Uint8Array(header.length + expected);
        packet.set(header, 0);
        packet.set(encoded.bytes, header.length);

        await writeInChunks(characteristic, packet, chunkSize || 20);
    }

    window.bontasticBitmap = {
        encodeBitmapFromFile,
        drawEncodedToCanvas,
        hexPreview,
        sendBitmap,
    };
})();
