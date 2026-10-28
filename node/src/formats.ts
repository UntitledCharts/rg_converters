import { check, native, optionsJson, present, snakeToCamel, takeBytes, takeJson, type Out, type Pointer } from './native';
import { Score } from './score';

/** The format ids shared by detect, the CLI, the Python API, the C library and this package. */
export type FormatId =
    | 'sus'
    | 'yume_sus'
    | 'holodori_sus'
    | 'usc'
    | 'mmws'
    | 'ccmmws'
    | 'unchmmws'
    | 'pjsk'
    | 'bdon'
    | 'next_sekai'
    | 'chart_cyanvas'
    | 'untitled_sekai';

export type FeatureId =
    | 'extended_lanes'
    | 'layers'
    | 'note_speed_ratios'
    | 'measure_extensions'
    | 'fake_notes'
    | 'damage_notes'
    | 'skill_fever'
    | 'holodori_skill_fever'
    | 'extended_eases'
    | 'extended_guide_colors'
    | 'guide_fades'
    | 'critical_end_on_plain_slide'
    | 'volume_changes';

export interface Feature {
    readonly id: FeatureId;
    /** Display name, e.g. "Extended Lanes". */
    readonly name: string;
}

/**
 * How a format holds a feature: what exporting does with it (for a load-only format, whether a
 * loaded chart can have it). 'lossy': kept approximately or in another form (note speed ratios as
 * layers); 'option': kept only with an export option.
 */
export type Support = 'yes' | 'option' | 'lossy' | 'no';
export type Operation = 'load' | 'export';
export type OptionValue = boolean | number | string;

export interface OptionInfo {
    /** As passed in options here, e.g. 'skipShift'. */
    readonly name: string;
    /** As the C library, the Python API and error messages name it, e.g. 'skip_shift'. */
    readonly nativeName: string;
    readonly type: 'bool' | 'int' | 'str';
    readonly required: boolean;
    /** null when required. */
    readonly default: OptionValue | null;
}

type NoOptions = Record<string, never>;

export interface ImportOptions {
    sus: { skipNoteSpeedRatios?: boolean };
    yume_sus: NoOptions;
    holodori_sus: NoOptions;
    usc: NoOptions;
    /** mmws, ccmmws and unchmmws each load only their own variant. */
    mmws: NoOptions;
    ccmmws: NoOptions;
    unchmmws: NoOptions;
    pjsk: NoOptions;
    bdon: { guideTraces?: boolean; traceEnds?: boolean; flickBodyCollide?: boolean };
    next_sekai: { errorOnUnsupportedFeature?: boolean; attemptAttachedNoteConvert?: boolean };
    chart_cyanvas: { errorOnUnsupportedFeature?: boolean };
}

/**
 * A format that holds one layer throws on a score with more unless stripLayers (sus: allowLayers)
 * is set.
 */
export interface ExportOptions {
    sus: {
        allowLayers?: boolean;
        allowDamage?: boolean;
        allowNoteSpeedRatios?: boolean;
        allowMeasureExtensions?: boolean;
        skipShift?: boolean;
    };
    yume_sus: { skipShift?: boolean; stripLayers?: boolean };
    usc: { minified?: boolean };
    mmws: { stripLayers?: boolean };
    ccmmws: NoOptions;
    unchmmws: NoOptions;
    /** compressed: base64(gzip(json)) as the game stores it (default); false writes plain JSON. */
    pjsk: { musicId: number; compressed?: boolean; stripLayers?: boolean };
    next_sekai: { compressed?: boolean; smoothGuideFade?: boolean; useGuideLayer?: boolean };
    chart_cyanvas: { compressed?: boolean };
    untitled_sekai: { compressed?: boolean };
}

export type LoadFormat = keyof ImportOptions;
export type ExportFormat = keyof ExportOptions;

type TextFormat = 'sus' | 'yume_sus' | 'usc';

/** sus, yume_sus and usc export text; the rest export bytes. */
export type ExportData<F extends ExportFormat> = F extends TextFormat ? string : Uint8Array;

/** What an export wrote, and the score as written (the score passed in is left as it is). */
export interface ExportResult<D extends string | Uint8Array = string | Uint8Array> {
    data: D;
    score: Score;
}

type OptionsArg<O> = {} extends O ? [options?: O] : [options: O];

/** Chart data: text (encoded as UTF-8) or bytes. */
export type Input = string | ArrayBuffer | ArrayBufferView;

interface NativeOption {
    name: string;
    type: OptionInfo['type'];
    required: boolean;
    default: OptionValue | null;
}

interface NativeFormat {
    id: FormatId;
    description: string;
    deprecated: boolean;
    binary: boolean;
    operations: Operation[];
    file_types: string[];
    features: Record<FeatureId, Support>;
    import_options: NativeOption[];
    export_options: NativeOption[];
}

interface FormatInfo {
    description: string;
    deprecated: boolean;
    binary: boolean;
    operations: readonly Operation[];
    fileTypes: readonly string[];
    features: Readonly<Record<FeatureId, Support>>;
    importOptions: readonly OptionInfo[];
    exportOptions: readonly OptionInfo[];
}

interface Registry {
    features: readonly Feature[];
    formats: ReadonlyMap<FormatId, FormatInfo>;
}

let registry: Registry | undefined;

function optionInfos(options: NativeOption[]): readonly OptionInfo[] {
    return Object.freeze(options.map((o) => Object.freeze({
        name: snakeToCamel(o.name), nativeName: o.name, type: o.type, required: o.required, default: o.default,
    })));
}

function loadRegistry(): Registry {
    if (registry) return registry;
    const out: Out<Pointer> = [null];
    check(native().formats(out));
    const json = takeJson<{ features: Feature[]; formats: NativeFormat[] }>(out);
    registry = {
        features: Object.freeze(json.features.map((f) => Object.freeze({ id: f.id, name: f.name }))),
        formats: new Map(json.formats.map((f) => [f.id, Object.freeze({
            description: f.description,
            deprecated: f.deprecated,
            binary: f.binary,
            operations: Object.freeze(f.operations),
            fileTypes: Object.freeze(f.file_types),
            features: Object.freeze(f.features),
            importOptions: optionInfos(f.import_options),
            exportOptions: optionInfos(f.export_options),
        })])),
    };
    return registry;
}

function infoOf(id: FormatId): FormatInfo {
    return present(loadRegistry().formats.get(id) ?? null);
}

/** A chart format and what the registry says about it; the objects in `formats` add load / export. */
export class Format<F extends FormatId = FormatId> {
    readonly id: F;

    /** @internal */
    constructor(id: F) {
        this.id = id;
    }

    get description(): string {
        return infoOf(this.id).description;
    }

    get deprecated(): boolean {
        return infoOf(this.id).deprecated;
    }

    /** The file is bytes (gzip, base64, binary); otherwise it is UTF-8 text. */
    get binary(): boolean {
        return infoOf(this.id).binary;
    }

    get operations(): readonly Operation[] {
        return infoOf(this.id).operations;
    }

    /** File extensions, e.g. ['.sus']. */
    get fileTypes(): readonly string[] {
        return infoOf(this.id).fileTypes;
    }

    get features(): Readonly<Record<FeatureId, Support>> {
        return infoOf(this.id).features;
    }

    get importOptions(): readonly OptionInfo[] {
        return infoOf(this.id).importOptions;
    }

    get exportOptions(): readonly OptionInfo[] {
        return infoOf(this.id).exportOptions;
    }

    supports(feature: FeatureId | Feature): Support {
        return this.features[typeof feature === 'string' ? feature : feature.id];
    }
}

function toBytes(data: Input): Uint8Array {
    if (typeof data === 'string') return Buffer.from(data, 'utf8');
    if (data instanceof Uint8Array) return data;
    if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    if (data instanceof ArrayBuffer) return new Uint8Array(data);
    throw new TypeError('chart data must be a string, an ArrayBuffer or a typed array');
}

export function load<F extends LoadFormat>(format: F, data: Input, ...[options]: OptionsArg<ImportOptions[F]>): Score {
    const bytes = toBytes(data);
    const score: Out<Pointer> = [null];
    check(native().load(format, bytes, bytes.byteLength, optionsJson(options), score, null));
    return Score._take(score);
}

export function exportScore<F extends ExportFormat>(
    format: F,
    score: Score,
    ...[options]: OptionsArg<ExportOptions[F]>
): ExportResult<ExportData<F>> {
    const data: Out<Pointer> = [null];
    const size: Out<number> = [null];
    const written: Out<Pointer> = [null];
    check(native().export(format, score._handle, optionsJson(options), data, size, written));
    const writtenScore = Score._take(written);
    const bytes = takeBytes(data, Number(present(size[0])));
    return {
        data: (infoOf(format).binary ? bytes : bytes.toString('utf8')) as ExportData<F>,
        score: writtenScore,
    };
}

/** Every format id the data could be, most likely first ([] when unrecognised). */
export function detect(data: Input): FormatId[] {
    const bytes = toBytes(data);
    const out: Out<Pointer> = [null];
    check(native().detect(bytes, bytes.byteLength, null, out));
    return takeJson<FormatId[]>(out);
}

/** Every feature a format can hold, in display order. */
export function features(): readonly Feature[] {
    return loadRegistry().features;
}

/** The features a score uses, in display order. */
export function featuresUsed(score: Score): Feature[] {
    const out: Out<Pointer> = [null];
    check(native().scoreQuery(score._handle, 'features_used', null, out));
    const used = new Set(takeJson<FeatureId[]>(out));
    return features().filter((f) => used.has(f.id));
}

export type Loader<F extends LoadFormat> = (data: Input, ...options: OptionsArg<ImportOptions[F]>) => Score;
export type Exporter<F extends ExportFormat> = (
    score: Score,
    ...options: OptionsArg<ExportOptions[F]>
) => ExportResult<ExportData<F>>;

type Operations<F extends FormatId> =
    & (F extends LoadFormat ? { load: Loader<F> } : {})
    & (F extends ExportFormat ? { export: Exporter<F> } : {});

/** A format with the operations it can do. */
export type FormatOf<F extends FormatId> = Format<F> & Operations<F>;

export type Formats = { readonly [F in FormatId]: FormatOf<F> };

function loader<F extends LoadFormat>(format: F): Loader<F> {
    return (data, ...options) => load(format, data, ...options);
}

function exporter<F extends ExportFormat>(format: F): Exporter<F> {
    return (score, ...options) => exportScore(format, score, ...options);
}

function both<F extends LoadFormat & ExportFormat>(id: F): Format<F> & { load: Loader<F>; export: Exporter<F> } {
    return Object.assign(new Format(id), { load: loader(id), export: exporter(id) });
}

/** Every format, keyed by id, in the registry's order. */
export const formats: Formats = Object.freeze({
    sus: both('sus'),
    yume_sus: both('yume_sus'),
    holodori_sus: Object.assign(new Format('holodori_sus'), { load: loader('holodori_sus') }),
    usc: both('usc'),
    mmws: both('mmws'),
    ccmmws: both('ccmmws'),
    unchmmws: both('unchmmws'),
    pjsk: both('pjsk'),
    bdon: Object.assign(new Format('bdon'), { load: loader('bdon') }),
    next_sekai: both('next_sekai'),
    chart_cyanvas: both('chart_cyanvas'),
    untitled_sekai: Object.assign(new Format('untitled_sekai'), { export: exporter('untitled_sekai') }),
});
