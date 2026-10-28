export type ErrorCode =
    | 'INVALID_ARGUMENT'
    | 'UNSUPPORTED'
    | 'INVALID_DATA'
    | 'UNSUPPORTED_FEATURE'
    | 'FAILED'
    | 'OUT_OF_MEMORY'
    | 'INTERNAL';

const CODES: readonly ErrorCode[] = [
    'INVALID_ARGUMENT',
    'UNSUPPORTED',
    'INVALID_DATA',
    'UNSUPPORTED_FEATURE',
    'FAILED',
    'OUT_OF_MEMORY',
    'INTERNAL',
];

/** An error reported by the native library; `message` is its message. */
export class RgError extends Error {
    readonly status: number;
    readonly code: ErrorCode;

    constructor(status: number, message: string) {
        super(message);
        this.name = 'RgError';
        this.status = status;
        this.code = CODES[status - 1] ?? 'INTERNAL';
    }
}

/** The file uses something a Score can't hold (next_sekai / chart_cyanvas `errorOnUnsupportedFeature`). */
export class UnsupportedFeatureError extends RgError {
    constructor(status: number, message: string) {
        super(status, message);
        this.name = 'UnsupportedFeatureError';
    }
}

export function errorFor(status: number, message: string): RgError {
    return CODES[status - 1] === 'UNSUPPORTED_FEATURE'
        ? new UnsupportedFeatureError(status, message)
        : new RgError(status, message);
}
