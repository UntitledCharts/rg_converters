import fs from 'node:fs';
import path from 'node:path';
import koffi from 'koffi';
import { errorFor } from './errors';

export type Pointer = bigint;
export type Out<T> = [T | null];

interface Api {
    version(): string;
    abiVersion(): number;
    lastError(): string;
    free(ptr: Pointer): void;
    scoreFree(score: Pointer): void;
    formats(outJson: Out<Pointer>): number;
    load(format: string, data: Uint8Array, size: number, options: string | null,
         outScore: Out<Pointer>, outInfo: Out<Pointer> | null): number;
    export(format: string, score: Pointer, options: string | null, outData: Out<Pointer>,
           outSize: Out<number>, outWritten: Out<Pointer>): number;
    transform(name: string, score: Pointer, options: string | null, outScore: Out<Pointer>,
              outInfo: Out<Pointer>): number;
    detect(data: Uint8Array, size: number, options: string | null, outJson: Out<Pointer>): number;
    scoreToJson(score: Pointer, options: string | null, outJson: Out<Pointer>): number;
    scoreFromJson(json: string, outScore: Out<Pointer>): number;
    scoreQuery(score: Pointer, name: string, args: string | null, outJson: Out<Pointer>): number;
}

const ABI_VERSION = 1;
export const LIBRARY_ENV = 'RG_CONVERTERS_LIBRARY';

function libraryPath(): string {
    const override = process.env[LIBRARY_ENV];
    if (override) return override;
    const { platform, arch } = process;
    const [dir, file] =
        platform === 'win32' ? [`win32-${arch}`, 'rg_converters.dll']
        : platform === 'darwin' ? ['darwin-universal', 'librg_converters.dylib']
        : [`${platform}-${arch}`, 'librg_converters.so'];
    const full = path.join(__dirname, '..', 'prebuilds', dir, file);
    if (!fs.existsSync(full)) {
        throw new Error(`rg-converters has no native library for ${platform}-${arch} (no ${full}); ` +
                        `set ${LIBRARY_ENV} to the path of one built from the repository's capi/`);
    }
    return full;
}

let api: Api | undefined;

export function native(): Api {
    if (api) return api;
    const lib = koffi.load(libraryPath());
    const loaded: Api = {
        version: lib.func('const char *rg_version(void)'),
        abiVersion: lib.func('int32_t rg_abi_version(void)'),
        lastError: lib.func('const char *rg_last_error(void)'),
        free: lib.func('void rg_free(void *ptr)'),
        scoreFree: lib.func('void rg_score_free(void *score)'),
        formats: lib.func('int rg_formats(_Out_ void **out_json)'),
        load: lib.func('int rg_load(const char *format, const uint8_t *data, size_t size, ' +
                       'const char *options, _Out_ void **out_score, _Out_ void **out_info)'),
        export: lib.func('int rg_export(const char *format, void *score, const char *options, ' +
                         '_Out_ void **out_data, _Out_ size_t *out_size, _Out_ void **out_written)'),
        transform: lib.func('int rg_transform(const char *name, void *score, const char *options, ' +
                            '_Out_ void **out_score, _Out_ void **out_info)'),
        detect: lib.func('int rg_detect(const uint8_t *data, size_t size, const char *options, ' +
                         '_Out_ void **out_json)'),
        scoreToJson: lib.func('int rg_score_to_json(void *score, const char *options, _Out_ void **out_json)'),
        scoreFromJson: lib.func('int rg_score_from_json(const char *json, _Out_ void **out_score)'),
        scoreQuery: lib.func('int rg_score_query(void *score, const char *name, const char *args, ' +
                             '_Out_ void **out_json)'),
    };
    const abi = loaded.abiVersion();
    if (abi !== ABI_VERSION) {
        throw new Error(`rg-converters needs native ABI ${ABI_VERSION}, the library has ${abi}`);
    }
    api = loaded;
    return api;
}

export function check(status: number): void {
    if (status !== 0) throw errorFor(status, native().lastError());
}

export function present<T>(value: T | null): T {
    if (value === null) throw new Error('rg-converters: the native library returned no value');
    return value;
}

export function takeString(out: Out<Pointer>): string {
    const ptr = present(out[0]);
    try {
        return koffi.decode.string(ptr);
    } finally {
        native().free(ptr);
    }
}

export function takeJson<T>(out: Out<Pointer>): T {
    return JSON.parse(takeString(out)) as T;
}

export function takeBytes(out: Out<Pointer>, size: number): Buffer {
    const ptr = present(out[0]);
    try {
        if (size === 0) return Buffer.alloc(0);
        const bytes = koffi.decode(ptr, 'uint8_t', size) as Uint8Array;
        return Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    } finally {
        native().free(ptr);
    }
}

const camelToSnake = (key: string): string => key.replace(/[A-Z]/g, (c) => `_${c.toLowerCase()}`);
export const snakeToCamel = (key: string): string => key.replace(/_([a-z])/g, (_, c: string) => c.toUpperCase());

/** JS options ({ skipShift: true }) as the native options JSON ({"skip_shift": true}). */
export function optionsJson(options: object | undefined): string | null {
    if (!options) return null;
    const entries = Object.entries(options).filter(([, v]) => v !== undefined);
    if (entries.length === 0) return null;
    return JSON.stringify(Object.fromEntries(entries.map(([k, v]) => [camelToSnake(k), v])));
}

export function version(): string {
    return native().version();
}
