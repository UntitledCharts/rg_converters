'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { describe, test } = require('node:test');

const rg = require('..');

const TEST_FILES = process.env.RG_TEST_FILES ?? path.join(__dirname, '..', '..', 'test_files');
const readTestFile = (name) => fs.readFileSync(path.join(TEST_FILES, name));
const sus88 = () => rg.load('sus', readTestFile('88s_append.sus'));
const layered = () => rg.Score.fromJSON({
    notes: [
        { kind: 'Bpm', beat: 0, bpm: 120 },
        { kind: 'TimeScaleGroup', changes: [{ beat: 0, timeScale: 1 }] },
        { kind: 'TimeScaleGroup', changes: [{ beat: 0, timeScale: 2 }] },
        { kind: 'Single', beat: 1, lane: 0, size: 1, timeScaleGroup: 0, critical: false, trace: false },
        { kind: 'Single', beat: 2, lane: 2, size: 1, timeScaleGroup: 1, critical: false, trace: false },
    ],
});
const expectFailure = (fn, message, code = 'FAILED') =>
    assert.throws(fn, (e) => e instanceof rg.RgError && e.code === code && message.test(e.message));

describe('library', () => {
    test('reports the package version', () => {
        assert.equal(rg.version(), require('../package.json').version);
    });

    test('loads every test file', () => {
        const pjskCombos = {
            '88s_append.pjsk.json': 1388,
            'empurple_append.pjsk.json': 1126,
            'intense_voice_full_append.pjsk.json': 4500,
        };
        for (const name of fs.readdirSync(TEST_FILES)) {
            const data = readTestFile(name);
            if (name.endsWith('.sus')) {
                assert.ok(rg.load('sus', data).comboCount > 0, name);
                assert.ok(rg.detect(data).includes('sus'), name);
            } else if (name in pjskCombos) {
                assert.equal(rg.formats.pjsk.load(data).comboCount, pjskCombos[name], name);
                assert.deepEqual(rg.detect(data), ['pjsk'], name);
            }
        }
    });

    test('accepts strings, buffers, array buffers and views', () => {
        const data = readTestFile('nouten_master.sus');
        const padded = Buffer.concat([Buffer.from('junk'), data]);
        const expected = rg.load('sus', data).comboCount;
        assert.equal(rg.load('sus', data.toString('utf8')).comboCount, expected);
        assert.equal(rg.load('sus', padded.subarray(4)).comboCount, expected);
        assert.equal(rg.load('sus', new Uint8Array(data).buffer).comboCount, expected);
        assert.throws(() => rg.load('sus', 42), TypeError);
    });
});

describe('exports', () => {
    const cases = [
        ['sus', 'sus', undefined],
        ['sus', 'sus', { allowLayers: true, skipShift: true }],
        ['sus', 'sus', { allowDamage: true, allowNoteSpeedRatios: true, allowMeasureExtensions: true }],
        ['yume_sus', 'yume_sus', undefined],
        ['yume_sus', 'yume_sus', { skipShift: true, stripLayers: true }],
        ['usc', 'usc', { minified: false }],
        ['mmws', 'mmws', undefined],
        ['mmws', 'mmws', { stripLayers: true }],
        ['ccmmws', 'ccmmws', undefined],
        ['unchmmws', 'unchmmws', undefined],
        ['pjsk', 'pjsk', { musicId: 3 }],
        ['pjsk', 'pjsk', { musicId: 3, compressed: false, stripLayers: true }],
        ['next_sekai', 'next_sekai', undefined],
        ['next_sekai', 'next_sekai', { compressed: false, smoothGuideFade: true }],
        ['chart_cyanvas', 'chart_cyanvas', { compressed: false }],
    ];

    for (const [format, loadFormat, options] of cases) {
        test(`${format} ${JSON.stringify(options ?? {})} round-trips`, () => {
            const score = sus88();
            const before = score.toJSONString();
            const { data, score: written } = rg.exportScore(format, score, options);
            assert.equal(typeof data === 'string', ['sus', 'yume_sus', 'usc'].includes(format));
            assert.ok(data.length > 0);
            assert.ok(rg.detect(data).includes(format), `${format} detected as ${rg.detect(data)}`);
            const loaded = rg.load(loadFormat, data);
            assert.equal(loaded.comboCount, score.comboCount);
            assert.equal(written.comboCount, loaded.comboCount);
            assert.equal(score.toJSONString(), before);
        });
    }

    test('formats namespace matches the generic functions', () => {
        const score = sus88();
        assert.equal(rg.formats.usc.export(score).data, rg.exportScore('usc', score).data);
        assert.deepEqual(rg.formats.next_sekai.export(score, { compressed: false }).data,
                         rg.exportScore('next_sekai', score, { compressed: false }).data);
        const untitled = rg.formats.untitled_sekai.export(score, { compressed: false });
        assert.ok(rg.detect(untitled.data).includes('untitled_sekai'));
        assert.equal(rg.formats.untitled_sekai.load, undefined);
        assert.equal(rg.formats.bdon.export, undefined);
    });

    test('pjsk can write plain JSON', () => {
        const { data } = rg.formats.pjsk.export(sus88(), { musicId: 7, compressed: false });
        assert.equal(JSON.parse(Buffer.from(data).toString('utf8')).MusicId, 7);
    });

    test('holodori_sus counts bezier ticks in formatMetadata', () => {
        const score = rg.formats.holodori_sus.load(readTestFile('88s_append.sus'));
        assert.equal(score.formatMetadata.format, 'holodori_sus');
        assert.equal(typeof score.formatMetadata.bezier_count, 'number');
    });

    test('one-layer formats need stripLayers for a score with layers', () => {
        const score = layered();
        expectFailure(() => rg.formats.mmws.export(score), /\.mmws holds one layer, but the score has 2/);
        expectFailure(() => rg.formats.pjsk.export(score, { musicId: 1 }), /strip_layers/);
        expectFailure(() => rg.formats.sus.export(score), /allow_layers/);
        assert.equal(rg.formats.mmws.load(rg.formats.mmws.export(score, { stripLayers: true }).data).comboCount, 2);
        assert.equal(rg.formats.sus.load(rg.formats.sus.export(score, { allowLayers: true }).data).comboCount, 2);
    });

    test('mmws variants only load their own files', () => {
        const { data } = rg.formats.ccmmws.export(sus88());
        assert.ok(rg.formats.ccmmws.load(data) instanceof rg.Score);
        expectFailure(() => rg.formats.mmws.load(data), /Not a \.mmws file/, 'INVALID_DATA');
    });
});

describe('transforms', () => {
    test('return new scores and leave the input alone', () => {
        const score = sus88();
        const before = score.toJSONString();
        for (const name of ['deleteFakeNotes', 'deleteDamageNotes', 'replaceExtendedGuideColors',
                            'replaceExtendedEase', 'sortByBeat', 'addPointWithoutFade', 'stripSpeedRatios',
                            'stripLayers', 'closeOpenSlides', 'flattenSpeedRatiosToLayers', 'shift',
                            'convertHolodoriEvents']) {
            const result = rg.transforms[name](score);
            assert.ok(result instanceof rg.Score, name);
            assert.notEqual(result, score);
        }
        assert.equal(rg.transforms.shift(score).comboCount, score.comboCount);
        assert.equal(rg.transforms.stripExtendedLanes(score, { resizeIfPossible: true }).comboCount, score.comboCount);
        assert.equal(score.toJSONString(), before);
    });

    test('stripLayers leaves one layer', () => {
        const score = layered();
        const stripped = rg.transforms.stripLayers(score);
        assert.deepEqual(rg.featuresUsed(score).map((f) => f.id), ['layers']);
        assert.deepEqual(rg.featuresUsed(stripped), []);
        const notes = stripped.toJSON().notes;
        assert.equal(notes.filter((n) => n.kind === 'TimeScaleGroup').length, 1);
        assert.ok(notes.every((n) => n.kind !== 'Single' || n.timeScaleGroup === 0));
        assert.equal(rg.formats.mmws.load(rg.formats.mmws.export(stripped).data).comboCount, 2);
    });

    test('cut and exportOverlapsScore return their extra results', () => {
        const score = sus88();
        const cut = rg.transforms.cut(score, { startAt: 10, endAt: 50 });
        assert.ok(cut.score.comboCount > 0 && cut.score.comboCount < score.comboCount);
        assert.equal(typeof cut.comboBefore, 'number');
        assert.deepEqual(cut.ticks, [14000, 70000]);
        const overlaps = rg.transforms.exportOverlapsScore(score);
        assert.ok(overlaps.score instanceof rg.Score);
        assert.equal(typeof overlaps.count, 'number');
    });
});

describe('format registry', () => {
    const ids = ['sus', 'yume_sus', 'holodori_sus', 'usc', 'mmws', 'ccmmws', 'unchmmws', 'pjsk', 'bdon', 'next_sekai',
                 'chart_cyanvas', 'untitled_sekai'];

    test('has every format, with its metadata', () => {
        assert.deepEqual(Object.keys(rg.formats), ids);
        for (const id of ids) {
            const format = rg.formats[id];
            assert.ok(format instanceof rg.Format, id);
            assert.equal(format.id, id);
            assert.ok(typeof format.description === 'string' && format.description.length > 0, id);
            assert.equal(typeof format.deprecated, 'boolean');
            assert.ok(format.fileTypes.length > 0 && format.fileTypes.every((t) => t.startsWith('.')), id);
            assert.deepEqual(Object.keys(format.features), rg.features().map((f) => f.id), id);
            assert.equal(typeof format.load === 'function', format.operations.includes('load'), id);
            assert.equal(typeof format.export === 'function', format.operations.includes('export'), id);
        }
        assert.match(rg.formats.sus.description, /classic old \.sus format/);
        assert.equal(rg.formats.usc.deprecated, true);
        assert.equal(rg.formats.sus.deprecated, false);
        assert.equal(rg.formats.sus.binary, false);
        assert.equal(rg.formats.pjsk.binary, true);
        assert.deepEqual(rg.formats.pjsk.fileTypes, ['.pjsk', '.json', '.json.gz']);
        assert.deepEqual(rg.formats.bdon.operations, ['load']);
        assert.deepEqual(rg.formats.untitled_sekai.operations, ['export']);
        assert.deepEqual(rg.formats.sus.operations, ['load', 'export']);
        assert.equal(rg.formats.sus.supports('layers'), 'option');
        assert.equal(rg.formats.sus.supports(rg.features()[0]), 'no');
        assert.equal(rg.formats.next_sekai.features.fake_notes, 'yes');
        assert.ok(Object.isFrozen(rg.formats.sus.fileTypes) && Object.isFrozen(rg.formats.sus.features));
    });

    test('lists the features', () => {
        const features = rg.features();
        assert.equal(features.length, 13);
        assert.deepEqual(features[0], { id: 'extended_lanes', name: 'Extended Lanes' });
        assert.deepEqual(rg.featuresUsed(rg.Score.fromJSON({})), []);
    });

    test('option names match the typed options', () => {
        const options = {
            sus: [['skipNoteSpeedRatios'],
                  ['allowLayers', 'allowDamage', 'allowNoteSpeedRatios', 'allowMeasureExtensions', 'skipShift']],
            yume_sus: [[], ['skipShift', 'stripLayers']],
            holodori_sus: [[], []],
            usc: [[], ['minified']],
            mmws: [[], ['stripLayers']],
            ccmmws: [[], []],
            unchmmws: [[], []],
            pjsk: [[], ['musicId', 'compressed', 'stripLayers']],
            bdon: [['guideTraces', 'traceEnds', 'flickBodyCollide'], []],
            next_sekai: [['errorOnUnsupportedFeature', 'attemptAttachedNoteConvert'],
                         ['compressed', 'smoothGuideFade', 'useGuideLayer']],
            chart_cyanvas: [['errorOnUnsupportedFeature'], ['compressed']],
            untitled_sekai: [[], ['compressed']],
        };
        for (const [id, [importNames, exportNames]] of Object.entries(options)) {
            assert.deepEqual(rg.formats[id].importOptions.map((o) => o.name), importNames, id);
            assert.deepEqual(rg.formats[id].exportOptions.map((o) => o.name), exportNames, id);
        }
        assert.deepEqual(rg.formats.pjsk.exportOptions[0],
                         { name: 'musicId', nativeName: 'music_id', type: 'int', required: true, default: null });
        assert.deepEqual(rg.formats.pjsk.exportOptions[1],
                         { name: 'compressed', nativeName: 'compressed', type: 'bool', required: false, default: true });
    });
});

describe('score model', () => {
    test('round-trips through JSON', () => {
        const score = sus88();
        const model = score.toJSON();
        assert.ok(model.notes.some((n) => n.kind === 'Slide' && n.connections[0].kind === 'SlideStartPoint'));
        const copy = rg.Score.fromJSON(model);
        assert.equal(copy.comboCount, score.comboCount);
        assert.equal(JSON.stringify(copy.toJSON()), JSON.stringify(model));
        assert.equal(rg.Score.fromJSON(score.toJSONString()).toJSONString(), score.toJSONString());
        assert.equal(JSON.stringify(score), JSON.stringify(model));
        assert.match(score.toJSONString(2), /^\{\n {2}"metadata": \{/);
    });

    test('carries format metadata', () => {
        const score = sus88();
        assert.deepEqual(score.formatMetadata, { format: 'sus' });
        assert.deepEqual(score.toJSON().format_metadata, { format: 'sus' });
        assert.deepEqual(rg.transforms.shift(score).formatMetadata, { format: 'sus' });
        const made = rg.Score.fromJSON({ format_metadata: { format: 'usc', n: 2, x: 0.5, b: true } });
        assert.deepEqual(made.formatMetadata, { b: true, format: 'usc', n: 2, x: 0.5 });
        assert.deepEqual(rg.Score.fromJSON({}).formatMetadata, {});
        expectFailure(() => rg.Score.fromJSON({ format_metadata: { format: [] } }),
                      /at format_metadata: "format" must be a boolean, a number or a string/, 'INVALID_ARGUMENT');
    });

    test('edits show up in queries and exports', () => {
        const model = sus88().toJSON();
        const combo = rg.Score.fromJSON(model).comboCount;
        model.metadata.title = 'テスト';
        model.notes.push(
            { kind: 'Single', beat: 2000, lane: 0, size: 1.5, timeScaleGroup: 0, critical: true, trace: false },
            { kind: 'HolodoriSkill', beat: 2001, slot: 3 },
        );
        model.notes = model.notes.filter((n) => n.kind !== 'Guide');
        const edited = rg.Score.fromJSON(model);
        assert.equal(edited.comboCount, combo + 1);
        const json = edited.toJSON();
        assert.equal(json.metadata.title, 'テスト');
        assert.deepEqual(json.notes.at(-2), {
            kind: 'Single', beat: 2000, critical: true, lane: 0, size: 1.5, fake: false, timeScaleGroup: 0,
            speedRatio: 1, trace: false, direction: null, type: 'single',
        });
        assert.deepEqual(json.notes.at(-1), { kind: 'HolodoriSkill', beat: 2001, slot: 3, type: 'holodoriSkill' });
        assert.ok(rg.exportScore('sus', edited).data.includes('#TITLE "テスト"'));
    });

    test('queries', () => {
        const score = rg.Score.fromJSON({
            notes: [
                { kind: 'Bpm', beat: 0, bpm: 120 },
                { kind: 'Single', beat: 1, lane: 0, size: 1, timeScaleGroup: 0, critical: true, direction: 'up' },
                { kind: 'Skill', beat: 1 },
                { kind: 'Skill', beat: 1 },
                {
                    kind: 'Slide', critical: false, connections: [
                        { kind: 'SlideStartPoint', beat: 2, critical: false, ease: 'linear', judgeType: 'normal',
                          lane: 0, size: 1, timeScaleGroup: 0 },
                        { kind: 'SlideEndPoint', beat: 3, critical: false, judgeType: 'normal', lane: 0, size: 1,
                          timeScaleGroup: 0 },
                    ],
                },
            ],
        });
        assert.equal(score.comboCount, 4);
        assert.equal(score.noteCount, 4);
        assert.equal(score.duration, 1.5);
        assert.equal(score.timeAtBeat(10), 5);
        assert.equal(score.beatAtTime(5), 10);
        assert.equal(score.checkSkillOverlap(), true);
        assert.deepEqual(score.comboEvents()[0], { category: 'flicks', critical: true, beat: 1 });
        assert.deepEqual(score.comboEvents().map((e) => e.category),
                         ['flicks', 'long_starts', 'long_ends', 'long_continuations']);
    });

    test('dispose frees the native score', () => {
        const score = sus88();
        score.dispose();
        score.dispose();
        assert.throws(() => score.comboCount, /disposed/);
    });
});

describe('errors', () => {
    const score = sus88();
    const expectError = (fn, code, message) =>
        assert.throws(fn, (e) => e instanceof rg.RgError && e.code === code && message.test(e.message));

    test('carry the native message and code', () => {
        expectError(() => rg.load('nope', ''), 'UNSUPPORTED', /unknown load format "nope"/);
        expectError(() => rg.load('mmws', Buffer.from([1, 2, 3])), 'INVALID_DATA', /signature/);
        expectError(() => rg.load('bdon', '{}', { traceEnds: 1 }), 'INVALID_ARGUMENT', /"trace_ends" must be a boolean/);
        expectError(() => rg.load('sus', '', { bogus: true }), 'INVALID_ARGUMENT',
                    /unknown option "bogus" \(known: skip_note_speed_ratios\)/);
        expectError(() => rg.exportScore('next_sekai', score, { asCompressed: false }), 'INVALID_ARGUMENT',
                    /unknown option "as_compressed"/);
        expectError(() => rg.exportScore('sus', score, { allowExtendedLanes: true }), 'INVALID_ARGUMENT',
                    /unknown option "allow_extended_lanes"/);
        expectError(() => rg.exportScore('pjsk', score, { musicId: 1.5 }), 'INVALID_ARGUMENT',
                    /"music_id" must be an integer/);
        expectError(() => rg.load('untitled_sekai', ''), 'UNSUPPORTED', /can only be exported/);
        expectError(() => rg.exportScore('bdon', score), 'UNSUPPORTED', /can only be loaded/);
        expectError(() => rg.exportScore('pjsk', score), 'INVALID_ARGUMENT', /"music_id" is required/);
        expectError(() => rg.formats.pjsk.export(score, { compressed: false }), 'INVALID_ARGUMENT',
                    /"music_id" is required/);
        expectError(() => rg.transforms.cut(score, {}), 'INVALID_ARGUMENT', /start_at/);
        expectError(() => rg.Score.fromJSON({ notes: [{ kind: 'Single', lane: 0, size: 1, timeScaleGroup: 0 }] }),
                    'INVALID_ARGUMENT', /at notes\[0\]: "beat" is required/);
        expectError(() => rg.Score.fromJSON('{'), 'INVALID_ARGUMENT', /not valid JSON/);
    });

    test('unsupported LevelData features raise UnsupportedFeatureError', () => {
        const levelData = JSON.stringify({ bgmOffset: 0, entities: [{ archetype: 'SomethingNew', data: [] }] });
        assert.throws(() => rg.formats.next_sekai.load(levelData),
                      (e) => e instanceof rg.UnsupportedFeatureError && /SomethingNew/.test(e.message));
        assert.ok(rg.formats.next_sekai.load(levelData, { errorOnUnsupportedFeature: false }) instanceof rg.Score);
    });
});

const yumeChart = process.env.RG_TEST_YUME_CHART;
describe('yume_sus chart', { skip: !yumeChart && 'set RG_TEST_YUME_CHART (and RG_TEST_YUME_COMBO)' }, () => {
    test('loads and round-trips', () => {
        const data = fs.readFileSync(yumeChart);
        const score = rg.formats.yume_sus.load(data);
        if (process.env.RG_TEST_YUME_COMBO) assert.equal(score.comboCount, Number(process.env.RG_TEST_YUME_COMBO));
        assert.ok(rg.detect(data).includes('yume_sus'));
        const { data: text } = rg.formats.yume_sus.export(score);
        assert.equal(rg.formats.yume_sus.load(text).comboCount, score.comboCount);
        assert.equal(rg.load('next_sekai', rg.exportScore('next_sekai', score).data).comboCount, score.comboCount);
    });
});
