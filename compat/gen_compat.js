/* 从旧名→新名映射表 + 项目源码实际使用的标识符，生成精确兼容头 joy_compat.h。
 * 用法：node compat/gen_compat.js
 * 类型映射全量采信 type_rename_map.json（可靠）。
 * 函数映射采信 rename_map.json，但只输出「项目源码确实使用」且「新名在源码中确有声明」
 * 的条目，避免链式历史名（如 joy_script_msgpack_*）造成链接失败。 */
const fs = require('fs');
const path = require('path');
const ROOT = path.join(__dirname, '..');
const JOY = path.join(ROOT, 'third_party', 'joy');

const typeMap = JSON.parse(fs.readFileSync(path.join(JOY, 'api/idl/type_rename_map.json'), 'utf8'));
const fnMap = JSON.parse(fs.readFileSync(path.join(JOY, 'api/idl/rename_map.json'), 'utf8'));

// 递归收集项目源码（排除 third_party、bench、build*、game_cs）
const srcFiles = [];
function walk(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (['third_party', 'bench', 'game_cs', '.git'].includes(e.name)) continue;
    if (/^build/.test(e.name)) continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p);
    else if (/\.(h|c|hpp|cpp)$/.test(e.name)) srcFiles.push(p);
  }
}
walk(ROOT);
let src = '';
for (const f of srcFiles) src += fs.readFileSync(f, 'utf8') + '\n';

// 收集新版 joy 源码中出现的所有标识符（用于验证新名确实存在）
let joySrc = '';
function walkJoy(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    if (['SDL', 'spine', 'msgpack', 'external', 'examples', 'runtimes'].includes(e.name)) continue;
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walkJoy(p);
    else if (/\.(h|c)$/.test(e.name)) joySrc += fs.readFileSync(p, 'utf8') + '\n';
  }
}
walkJoy(JOY);

function used(word) { return new RegExp('\\b' + word + '\\b').test(src); }
function newExists(word) { return joySrc.includes(word); }

const typeLines = [];
for (const [old, neu] of Object.entries(typeMap)) {
  if (used(old) && !old.startsWith('joy_')) typeLines.push([old, neu]);
}
/* rename_map.json 记录的是链式历史名（如 joy_script_msgpack_* / joy_math_gjk3df_*），
 * 与当前源码真实名不符。这里以源码为准给出权威覆盖，优先级最高。 */
const FN_OVERRIDE = {
  // msgpack → codec 域
  jmsgpack_packer_init: 'joy_codec_msgpack_packer_init',
  jmsgpack_pack_int: 'joy_codec_msgpack_pack_int',
  jmsgpack_pack_int64: 'joy_codec_msgpack_pack_int64',
  jmsgpack_pack_uint: 'joy_codec_msgpack_pack_uint',
  jmsgpack_pack_uint64: 'joy_codec_msgpack_pack_uint64',
  jmsgpack_pack_nil: 'joy_codec_msgpack_pack_nil',
  jmsgpack_pack_array: 'joy_codec_msgpack_pack_array',
  jmsgpack_pack_str: 'joy_codec_msgpack_pack_str',
  jmsgpack_pack_str_body: 'joy_codec_msgpack_pack_str_body',
  jmsgpack_unpacked_init: 'joy_codec_msgpack_unpacked_init',
  jmsgpack_unpacked_destroy: 'joy_codec_msgpack_unpacked_destroy',
  jmsgpack_unpack_next: 'joy_codec_msgpack_unpack_next',
  jmsgpack_unpacked_data: 'joy_codec_msgpack_unpacked_data',
  jmsgpack_object_type: 'joy_codec_msgpack_object_type',
  jmsgpack_object_array_size: 'joy_codec_msgpack_object_array_size',
  jmsgpack_object_array_at: 'joy_codec_msgpack_object_array_at',
  jmsgpack_object_str_size: 'joy_codec_msgpack_object_str_size',
  jmsgpack_object_str_ptr: 'joy_codec_msgpack_object_str_ptr',
  jmsgpack_object_u64: 'joy_codec_msgpack_object_u64',
  jmsgpack_object_i64: 'joy_codec_msgpack_object_i64',
  jmsgpack_object_boolean: 'joy_codec_msgpack_object_boolean',
  jmsgpack_object_f64: 'joy_codec_msgpack_object_f64',
  // gjk / 3df 碰撞 → collision 域
  gjk3df_collide: 'joy_collision_gjk3df_collide',
  gjk3df_init_sphere: 'joy_collision_gjk3df_init_sphere',
  gjk3df_init_box: 'joy_collision_gjk3df_init_box',
  gjk3df_init_obb: 'joy_collision_gjk3df_init_obb',
  gjk3df_init_mesh: 'joy_collision_gjk3df_init_mesh',
  collision3df_get_ray_obb: 'joy_collision_3df_get_ray_obb',
  // 系统/计时器 → core / sys 域
  get_ticks: 'joy_core_get_ticks',
  game_timer_init: 'joy_core_game_timer_init',
  game_timer_update: 'joy_core_game_timer_update',
  game_timer_get_delta_time: 'joy_core_game_timer_get_delta_time',
  game_timer_set_target_fps: 'joy_core_game_timer_set_target_fps',
  get_local_ips: 'joy_sys_get_local_ips',
  sys_init_netenv: 'joy_sys_init_netenv',
};

const fnLines = [];
const emitted = new Set();
for (const [old, neu] of Object.entries(FN_OVERRIDE)) {
  if (used(old)) { fnLines.push([old, neu]); emitted.add(old); }
}
for (const [old, neu] of Object.entries(fnMap)) {
  if (!used(old) || old.startsWith('joy_') || emitted.has(old)) continue;
  if (newExists(neu)) fnLines.push([old, neu]);
}

const out = [];
out.push('/************************************************');
out.push(' * joy_compat.h —— 旧版 joy API → 新版 joy API 兼容层（自动生成）');
out.push(' * 由 compat/gen_compat.js 基于 type_rename_map.json / rename_map.json 生成。');
out.push(' * 经 CMake /FI 强制注入每个源文件，零改动业务源码适配新版引擎。');
out.push(' *************************************************/');
out.push('#ifndef PROJECT_PROXY3D_JOY_COMPAT_H');
out.push('#define PROJECT_PROXY3D_JOY_COMPAT_H');
out.push('');
out.push('/* ---- 类型映射 ---- */');
for (const [o, n] of typeLines) out.push(`#define ${o} ${n}`);
out.push('');
out.push('/* ---- 函数映射（已验证新名在新版源码存在） ---- */');
for (const [o, n] of fnLines) out.push(`#define ${o} ${n}`);
out.push('');
out.push('/* ---- 常量宏（type_rename_map 未收录，手工维护） ---- */');
const CONST_MACROS = {
  FP_FRACTIONAL_PLACES: 'JOY_FP_FRACTIONAL_PLACES',
  FP_ONE: 'JOY_FP_ONE',
  FP_PI: 'JOY_FP_PI',
  FP_MAX_VALUE: 'JOY_FP_MAX_VALUE',
  FP_MIN_VALUE: 'JOY_FP_MIN_VALUE',
};
for (const [o, n] of Object.entries(CONST_MACROS)) {
  if (used(o)) out.push(`#define ${o} ${n}`);
}
out.push('');
out.push('#endif /* PROJECT_PROXY3D_JOY_COMPAT_H */');

fs.writeFileSync(path.join(__dirname, 'joy_compat.h'), out.join('\n') + '\n', 'utf8');
console.log('types', typeLines.length, 'functions', fnLines.length);
console.log('unmapped-but-used funcs (new name missing in joy src):');
for (const [old, neu] of Object.entries(fnMap)) {
  if (used(old) && !old.startsWith('joy_') && !newExists(neu) && !FN_OVERRIDE[old]) console.log('  ', old, '->', neu);
}
