// The Score JSON model of the C library (capi/include/rg_converters.h): Python's classes and field
// names, with "kind" naming the class.

import type { FormatId } from './formats';

export interface MetaData {
    title: string;
    artist: string;
    designer: string;
    /** Offset in seconds; negative is late, positive early. */
    waveoffset: number;
    requests: string[] | null;
}

export interface Bpm {
    kind: 'Bpm';
    beat: number;
    bpm: number;
    type: string;
}

export interface TimeScalePoint {
    kind: 'TimeScalePoint';
    beat: number;
    timeScale: number;
}

export interface TimeScaleGroup {
    kind: 'TimeScaleGroup';
    changes: TimeScalePoint[];
    type: string;
}

export interface Volume {
    kind: 'Volume';
    beat: number;
    volume: number;
    type: string;
}

interface Event<K extends string> {
    kind: K;
    beat: number;
    type: string;
}

export type Skill = Event<'Skill'>;
export type FeverChance = Event<'FeverChance'>;
export type FeverStart = Event<'FeverStart'>;
export type HolodoriChargeStart = Event<'HolodoriChargeStart'>;
export type HolodoriChargeEnd = Event<'HolodoriChargeEnd'>;
export type HolodoriFeverStart = Event<'HolodoriFeverStart'>;
export type HolodoriFeverEnd = Event<'HolodoriFeverEnd'>;

export interface HolodoriSkill extends Event<'HolodoriSkill'> {
    /** Deck position (1-5) whose special skill fires here. */
    slot: number;
}

export interface Single {
    kind: 'Single';
    beat: number;
    critical: boolean | null;
    lane: number;
    size: number;
    fake: boolean;
    timeScaleGroup: number;
    speedRatio: number;
    trace: boolean | null;
    direction: string | null;
    type: string;
}

export interface SlideStartPoint {
    kind: 'SlideStartPoint';
    beat: number;
    critical: boolean;
    ease: string;
    judgeType: string;
    lane: number;
    size: number;
    timeScaleGroup: number;
    speedRatio: number;
    type: string;
}

export interface SlideRelayPoint {
    kind: 'SlideRelayPoint';
    beat: number;
    ease: string;
    lane: number;
    size: number;
    timeScaleGroup: number;
    type: 'tick' | 'attach' | (string & {});
    critical: boolean | null;
    fake: boolean;
    speedRatio: number;
}

export interface SlideEndPoint {
    kind: 'SlideEndPoint';
    beat: number;
    critical: boolean;
    judgeType: string;
    lane: number;
    size: number;
    timeScaleGroup: number;
    speedRatio: number;
    direction: string | null;
    type: string;
}

export type SlidePoint = SlideStartPoint | SlideRelayPoint | SlideEndPoint;

export interface Slide {
    kind: 'Slide';
    critical: boolean;
    fake: boolean;
    connections: SlidePoint[];
    type: string;
}

export interface GuidePoint {
    kind: 'GuidePoint';
    beat: number;
    ease: string;
    lane: number;
    size: number;
    timeScaleGroup: number;
    speedRatio: number;
    /** Alpha keyframe (0..1); a guide with none fades by its `fade`. */
    alpha: number | null;
}

export interface Guide {
    kind: 'Guide';
    color: string;
    fade: string;
    midpoints: GuidePoint[];
    type: string;
    color_code: string | null;
}

export type Note =
    | Bpm
    | TimeScaleGroup
    | TimeScalePoint
    | Volume
    | Skill
    | FeverChance
    | FeverStart
    | HolodoriSkill
    | HolodoriChargeStart
    | HolodoriChargeEnd
    | HolodoriFeverStart
    | HolodoriFeverEnd
    | Single
    | Slide
    | SlidePoint
    | Guide
    | GuidePoint;

/** What loading the file told us beyond the chart. */
export interface FormatMetadata {
    /** The id of the format the score was loaded as. */
    format?: FormatId | (string & {});
    /** holodori_sus: how many bezier ticks the chart has. */
    bezier_count?: number;
    [key: string]: boolean | number | string | undefined;
}

export interface ScoreModel {
    metadata: MetaData;
    format_metadata: FormatMetadata;
    notes: Note[];
}

// What Score.fromJSON accepts: the same, with the fields that have defaults optional.

type Defaults<T, K extends keyof T> = Omit<T, K> & Partial<Pick<T, K>>;

export type TimeScalePointInput = Defaults<TimeScalePoint, 'kind'>;
export type GuidePointInput = Defaults<GuidePoint, 'kind' | 'speedRatio' | 'alpha'>;
export type SlideStartPointInput = Defaults<SlideStartPoint, 'speedRatio' | 'type'>;
export type SlideRelayPointInput = Defaults<SlideRelayPoint, 'critical' | 'fake' | 'speedRatio'>;
export type SlideEndPointInput = Defaults<SlideEndPoint, 'speedRatio' | 'direction' | 'type'>;
export type SlidePointInput = SlideStartPointInput | SlideRelayPointInput | SlideEndPointInput;

export type NoteInput =
    | Defaults<Bpm, 'type'>
    | (Defaults<Omit<TimeScaleGroup, 'changes'>, 'type'> & { changes?: TimeScalePointInput[] })
    | TimeScalePoint
    | Defaults<Volume, 'type'>
    | Defaults<Skill, 'type'>
    | Defaults<FeverChance, 'type'>
    | Defaults<FeverStart, 'type'>
    | Defaults<HolodoriSkill, 'type'>
    | Defaults<HolodoriChargeStart, 'type'>
    | Defaults<HolodoriChargeEnd, 'type'>
    | Defaults<HolodoriFeverStart, 'type'>
    | Defaults<HolodoriFeverEnd, 'type'>
    | Defaults<Single, 'critical' | 'fake' | 'speedRatio' | 'trace' | 'direction' | 'type'>
    | (Defaults<Omit<Slide, 'connections'>, 'fake' | 'type'> & { connections: SlidePointInput[] })
    | SlidePointInput
    | (Defaults<Omit<Guide, 'midpoints'>, 'type' | 'color_code'> & { midpoints: GuidePointInput[] })
    | (GuidePointInput & { kind: 'GuidePoint' });

export interface ScoreInput {
    metadata?: Partial<MetaData> | null;
    format_metadata?: FormatMetadata | null;
    notes?: NoteInput[];
}
