import * as rg from '..';

declare const data: Uint8Array;

const score: rg.Score = rg.load('sus', data);
rg.load('sus', data, { skipNoteSpeedRatios: true });
rg.load('bdon', data, { guideTraces: false, traceEnds: false, flickBodyCollide: false });
rg.formats.next_sekai.load(data, { errorOnUnsupportedFeature: false, attemptAttachedNoteConvert: false });
const text: string = rg.exportScore('sus', score, { skipShift: true, allowLayers: true, allowDamage: true }).data;
rg.exportScore('sus', score, { allowNoteSpeedRatios: true, allowMeasureExtensions: true });
const bytes: Uint8Array = rg.exportScore('ccmmws', score).data;
const written: rg.Score = rg.formats.pjsk.export(score, { musicId: 1 }).score;
rg.formats.pjsk.export(score, { musicId: 1, compressed: false, stripLayers: true });
rg.formats.yume_sus.export(score, { skipShift: true, stripLayers: true });
rg.formats.mmws.export(score, { stripLayers: true });
rg.formats.next_sekai.export(score, { compressed: false, smoothGuideFade: true, useGuideLayer: true });
rg.formats.chart_cyanvas.export(score, { compressed: false });
rg.formats.untitled_sekai.export(score, { compressed: false });
const ids: rg.FormatId[] = rg.detect(text);
const plain: rg.Score = rg.formats.holodori_sus.load(data);
const meta: rg.FormatMetadata = plain.formatMetadata;
const bezier: number | undefined = meta.bezier_count;
const cut: rg.CutResult = rg.transforms.cut(score, { startAt: 1 });
const stripped: rg.Score = rg.transforms.stripLayers(score);
const model: rg.ScoreModel = score.toJSON();
const loadedAs: string | undefined = model.format_metadata.format;
const first: rg.Note | undefined = model.notes[0];
rg.Score.fromJSON({ notes: [{ kind: 'Single', beat: 1, lane: 0, size: 1, timeScaleGroup: 0 }] });
rg.Score.fromJSON({ format_metadata: { format: 'sus', bezier_count: 3 } });

const format: rg.Format = rg.formats.sus;
const description: string = format.description;
const deprecated: boolean = format.deprecated;
const operations: readonly rg.Operation[] = rg.formats.bdon.operations;
const fileTypes: readonly string[] = rg.formats.pjsk.fileTypes;
const support: rg.Support = rg.formats.sus.supports('layers');
const featureSupport: rg.Support = rg.formats.usc.features.extended_lanes;
const options: readonly rg.OptionInfo[] = rg.formats.pjsk.exportOptions;
const features: readonly rg.Feature[] = rg.features();
const used: rg.Feature[] = rg.featuresUsed(score);
const usedId: rg.FeatureId | undefined = used[0]?.id;

// @ts-expect-error musicId is required
rg.exportScore('pjsk', score);
// @ts-expect-error musicId is required
rg.formats.pjsk.export(score, { compressed: false });
// @ts-expect-error musicId is a number
rg.formats.pjsk.export(score, { musicId: '1' });
// @ts-expect-error renamed to compressed
rg.exportScore('next_sekai', score, { asCompressed: true });
// @ts-expect-error not a sus export option
rg.exportScore('sus', score, { allowExtendedLanes: true });
// @ts-expect-error ccmmws has no export options
rg.formats.ccmmws.export(score, { stripLayers: true });
// @ts-expect-error holodori_sus has no load options
rg.formats.holodori_sus.load(data, { countBezier: true });
// @ts-expect-error untitled_sekai can only be exported
rg.load('untitled_sekai', data);
// @ts-expect-error untitled_sekai can only be exported
rg.formats.untitled_sekai.load(data);
// @ts-expect-error bdon can only be loaded
rg.formats.bdon.export(score);
// @ts-expect-error not a feature id
rg.formats.sus.supports('lasers');
// @ts-expect-error a Single needs a beat
rg.Score.fromJSON({ notes: [{ kind: 'Single', lane: 0, size: 1, timeScaleGroup: 0 }] });

export {
    bezier, bytes, cut, deprecated, description, featureSupport, features, fileTypes, first, ids, loadedAs,
    operations, options, plain, stripped, support, used, usedId, written,
};
