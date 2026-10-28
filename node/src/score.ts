import { check, native, present, takeJson, takeString, type Out, type Pointer } from './native';
import type { FormatMetadata, ScoreInput, ScoreModel } from './model';

export type ComboCategory =
    | 'taps'
    | 'flicks'
    | 'long_starts'
    | 'long_ends'
    | 'long_flick_ends'
    | 'long_relays'
    | 'long_continuations';

export interface ComboEvent {
    category: ComboCategory;
    critical: boolean;
    beat: number;
}

const registry = new FinalizationRegistry<Pointer>((handle) => native().scoreFree(handle));

/**
 * A chart held by the native library. It never changes: loaders, exports and transforms return new
 * scores. Read or edit its contents through toJSON() / Score.fromJSON().
 */
export class Score {
    #handle: Pointer | null;

    private constructor(handle: Pointer) {
        this.#handle = handle;
        registry.register(this, handle, this);
    }

    /** @internal */
    static _take(out: Out<Pointer>): Score {
        return new Score(present(out[0]));
    }

    /** @internal */
    get _handle(): Pointer {
        if (this.#handle === null) throw new Error('this Score has been disposed');
        return this.#handle;
    }

    /**
     * A new score from the JSON model (or its text). JS numbers don't keep 1.0 apart from 1, so
     * whole floats come back as ints; only the formatting of JSON-based exports can differ.
     */
    static fromJSON(model: ScoreInput | ScoreModel | string): Score {
        const out: Out<Pointer> = [null];
        check(native().scoreFromJson(typeof model === 'string' ? model : JSON.stringify(model), out));
        return Score._take(out);
    }

    /** The whole score as the JSON model (a new object every call). */
    toJSON(): ScoreModel {
        return JSON.parse(this.toJSONString()) as ScoreModel;
    }

    toJSONString(indent?: number): string {
        const out: Out<Pointer> = [null];
        check(native().scoreToJson(this._handle, indent === undefined ? null : JSON.stringify({ indent }), out));
        return takeString(out);
    }

    /** What loading the file told us beyond the chart ({} for a score not loaded from a file). */
    get formatMetadata(): FormatMetadata {
        return this.#query('format_metadata');
    }

    get comboCount(): number {
        return this.#query('combo_count');
    }

    get noteCount(): number {
        return this.#query('note_count');
    }

    /** Seconds until the last note. */
    get duration(): number {
        return this.#query('duration');
    }

    timeAtBeat(targetBeat: number): number {
        return this.#query('time_at_beat', { target_beat: targetBeat });
    }

    beatAtTime(targetTime: number): number {
        return this.#query('beat_at_time', { target_time: targetTime });
    }

    comboEvents(): ComboEvent[] {
        return this.#query<[ComboCategory, boolean, number][]>('combo_events')
            .map(([category, critical, beat]) => ({ category, critical, beat }));
    }

    checkSkillOverlap(): boolean {
        return this.#query('check_skill_overlap');
    }

    /** Frees the native score now instead of when it is garbage collected. */
    dispose(): void {
        if (this.#handle === null) return;
        registry.unregister(this);
        native().scoreFree(this.#handle);
        this.#handle = null;
    }

    #query<T>(name: string, args?: object): T {
        const out: Out<Pointer> = [null];
        check(native().scoreQuery(this._handle, name, args ? JSON.stringify(args) : null, out));
        return takeJson<T>(out);
    }
}
