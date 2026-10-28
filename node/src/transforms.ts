import { check, native, optionsJson, takeJson, type Out, type Pointer } from './native';
import { Score } from './score';

// Score -> Score functions; each returns a new score and leaves the one passed in as it is.

function apply<I>(name: string, score: Score, options?: object): { score: Score; info: I } {
    const out: Out<Pointer> = [null];
    const info: Out<Pointer> = [null];
    check(native().transform(name, score._handle, optionsJson(options), out, info));
    return { score: Score._take(out), info: takeJson<I>(info) };
}

const run = (name: string, score: Score): Score => apply(name, score).score;

export function deleteFakeNotes(score: Score): Score {
    return run('delete_fake_notes', score);
}

export function deleteDamageNotes(score: Score): Score {
    return run('delete_damage_notes', score);
}

export function replaceExtendedGuideColors(score: Score): Score {
    return run('replace_extended_guide_colors', score);
}

export function replaceExtendedEase(score: Score): Score {
    return run('replace_extended_ease', score);
}

export function sortByBeat(score: Score): Score {
    return run('sort_by_beat', score);
}

export function addPointWithoutFade(score: Score): Score {
    return run('add_point_without_fade', score);
}

export function stripSpeedRatios(score: Score): Score {
    return run('strip_speed_ratios', score);
}

/** Keeps the first layer (timescale group) and moves every note onto it; note speed ratios stay. */
export function stripLayers(score: Score): Score {
    return run('strip_layers', score);
}

/** Ends every slide that has no end with an unjudged one a tick after its last point. */
export function closeOpenSlides(score: Score): Score {
    return run('close_open_slides', score);
}

export function flattenSpeedRatiosToLayers(score: Score): Score {
    return run('flatten_speed_ratios_to_layers', score);
}

export function shift(score: Score): Score {
    return run('shift', score);
}

/** Holodori events replaced with their sekai equivalents (section ends dropped). */
export function convertHolodoriEvents(score: Score): Score {
    return run('convert_holodori_events', score);
}

export function stripExtendedLanes(score: Score, options?: { resizeIfPossible?: boolean }): Score {
    return apply('strip_extended_lanes', score, options).score;
}

/** The overlapping notes as their own score, and how many overlaps there are. */
export function exportOverlapsScore(score: Score): { score: Score; count: number } {
    const { score: overlaps, info } = apply<{ count: number }>('export_overlaps_score', score);
    return { score: overlaps, count: info.count };
}

export interface CutOptions {
    /** Seconds; at least one of startAt and endAt is required. */
    startAt?: number | null;
    endAt?: number | null;
    keepPosition?: boolean;
}

export interface CutResult {
    score: Score;
    comboBefore: number;
    ticks: [start: number, end: number];
}

export function cut(score: Score, options: CutOptions): CutResult {
    const { score: result, info } = apply<{ combo_before: number; ticks: [number, number] }>('cut', score, options);
    return { score: result, comboBefore: info.combo_before, ticks: info.ticks };
}
