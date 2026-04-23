#ifndef CALLSIGN_H
#define CALLSIGN_H

/*
 * コールサインバリデータ / 正規化ユーティリティ
 *
 * 許容フォーマット: BASE[/XXX][-SSID]
 *   BASE : PREFIX(1〜2英数字) + AREA(1桁数字) + SUFFIX(1〜5英数字)  ← ITU-R M.1033構造
 *   /XXX : 任意。スラッシュ + 英数字1〜3文字（ポータブル等）
 *   -N   : 任意。ハイフン + 1〜2桁の数字（SSID: 0〜15）
 * 総長  : 最大15文字（終端NULLを除く）、最小3文字
 * 先頭・末尾に記号不可、上記以外の記号不可
 *
 * バッファサイズ: CALLSIGN_BUFSIZE (16バイト = 15文字 + NULL)
 */

#define CALLSIGN_BUFSIZE 16

/**
 * @brief コールサインを大文字に正規化する（インプレース）
 *
 * @param call 正規化する文字列（NULL 終端、インプレース書き換え）
 */
void callsign_normalize(char *call);

/**
 * @brief コールサインの妥当性チェック
 *
 * ITU-R M.1033 構造 (PREFIX + AREA + SUFFIX) に基づいて検証する。
 * オプションで /secondary および -SSID(0〜15) を許容する。
 *
 * @param call 検証するコールサイン文字列（NULL 終端）
 * @return 1: 正当, 0: 不正
 */
int callsign_validate(const char *call);

/**
 * コールサイン文字列をAX.25 / メタデータ用に分解する
 *
 * 入力例: "JH1FBM/P-3" -> out_base="JH1FBM", *out_ssid=3
 *         "VK2FABC"    -> out_base="VK2FAB", *out_ssid=0  (6文字で切り捨て)
 *         "W1AW-9"     -> out_base="W1AW",   *out_ssid=9
 *
 * @param call      入力コールサイン（正規化済み推奨）
 * @param out_base  出力: BASE先頭6文字 + ヌル終端 (7バイト以上のバッファ)
 * @param out_ssid  出力: SSID (0〜15、指定なしは0)
 */
void callsign_to_ax25(const char *call, char out_base[7], int *out_ssid);

#endif /* CALLSIGN_H */
