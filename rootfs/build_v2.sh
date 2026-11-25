#!/bin/bash
# =============================================================================
# build_v2.sh - reproducible build of the custom "v2" rootfs
# Xiaomi MJSXJ02CM (chuangmi.camera.ipc009) - MStar MSC000A - Linux 3.18.30
# ROOTFS partition = mtd2, size 0x750000, squashfs 4.0 xz, 128K blocks
#
# Run as root on a native Linux filesystem (ext4):
#   sudo STOCK_IMG=<stock.bin> TS_IMG=<rootfs_new.bin> bash build_v2.sh
#
# The source trees are extracted to $WORK (/var/tmp) and must stay on a
# filesystem that keeps UNIX modes and uid/gid and allows mknod(). A Windows
# drive mounted in WSL (DrvFs/NTFS) does not: an image built there has every
# file 0777 owned by the build user and no /dev/console, and does not boot.
#
# Inputs (environment variables, defaults in this directory):
#   STOCK_IMG  stock 4.3.7 rootfs (default: ./rootfs_stock_437.bin)
#   TS_IMG     tsunglung rootfs_new.bin (default: ./rootfs_new.bin)
#
# Outputs (in this directory):
#   custom_rootfs_v2.squashfs/.img/.md5              v2 "full tsunglung" (rebuilt)
#   custom_rootfs_v2_min.squashfs/.img/.md5          v2 "minimal" (stock 4.3.7 + telnetd + hook)
#   custom_rootfs_v2_orig_tsunglung.img/.md5         original tsunglung image, padded only (reference)
#   BUILD_LOG.txt                                    build log and verification results
# =============================================================================
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)

STOCK_IMG=${STOCK_IMG:-$HERE/rootfs_stock_437.bin}
TS_IMG=${TS_IMG:-$HERE/rootfs_new.bin}
WORK=${WORK:-/var/tmp/fw_v2_build}
PART_SIZE=$((0x750000))                 # 7,667,712 bytes (mtd2)
SQUASHFS_MAGIC="hsqs"

# mksquashfs options reproduce the stock rootfs profile EXACTLY
# (squashfs 4.0, xz, 128K blocks, fragments, duplicates, exports, default xattrs)
# -> superblock flags = 0x00c0, same as the stock and original tsunglung images.
# -no-xattrs/-no-exports are NOT used (they set flags to 0x0240, which is not
# the format of the images that boot), nor -all-root (the original uid/gid
# 0:0 and www-data:www-data are kept, as in the stock image).
MKSQ_OPTS=(-comp xz -b 131072 -noappend -no-sparse -no-progress)

log()  { echo "[build_v2] $*"; }
die()  { echo "[build_v2] ERROR: $*" >&2; exit 1; }

# -----------------------------------------------------------------------------
# 0. Prerequisites
# -----------------------------------------------------------------------------
[ "$(id -u)" -eq 0 ] || die "run as root: sudo STOCK_IMG=<stock.bin> TS_IMG=<rootfs_new.bin> bash $0"
for t in mksquashfs unsquashfs truncate md5sum od stat diff find cp chmod chown; do
    command -v "$t" >/dev/null 2>&1 || die "missing tool: $t"
done
[ -f "$STOCK_IMG" ] || die "stock rootfs not found: $STOCK_IMG"
[ -f "$TS_IMG" ]    || die "tsunglung rootfs not found: $TS_IMG"

# Reproducibility: reuse the mkfs timestamp of the reference image
# (superblock field s_mkfs_time, offset 0x08). With the same mksquashfs and the
# same source tree, the output image is then byte-for-byte stable.
STOCK_TIME=$(od -An -tu4 -j8 -N4 "$STOCK_IMG" | tr -d ' ')
TS_TIME=$(od -An -tu4 -j8 -N4 "$TS_IMG" | tr -d ' ')

rm -rf "$WORK"
mkdir -p "$WORK"
LOG="$HERE/BUILD_LOG.txt"
: > "$LOG"

{
    echo "=================================================================="
    echo " build_v2.sh - $(date -u '+%Y-%m-%d %H:%M:%SZ')"
    echo " host: $(uname -a)"
    echo " $(mksquashfs -version | head -1)"
    echo " stock : $STOCK_IMG ($(stat -c%s "$STOCK_IMG") B, md5 $(md5sum "$STOCK_IMG" | cut -d' ' -f1))"
    echo " tsungl: $TS_IMG ($(stat -c%s "$TS_IMG") B, md5 $(md5sum "$TS_IMG" | cut -d' ' -f1))"
    echo " work  : $WORK   |  mksquashfs options: ${MKSQ_OPTS[*]} -mkfs-time <ref> -quiet"
    echo " mkfs-time ref : stock=$STOCK_TIME ($(date -u -d @"$STOCK_TIME" '+%Y-%m-%d %H:%M:%S UTC'))  tsunglung=$TS_TIME ($(date -u -d @"$TS_TIME" '+%Y-%m-%d %H:%M:%S UTC'))"
    echo "=================================================================="
} | tee -a "$LOG"

# -----------------------------------------------------------------------------
# 1. Extract both reference trees on ext4 (as root => modes/device nodes OK)
# -----------------------------------------------------------------------------
log "extracting stock 4.3.7 rootfs -> $WORK/stock_tree"
unsquashfs -d "$WORK/stock_tree" "$STOCK_IMG" >"$WORK/unsquash_stock.log" 2>&1 \
    || die "unsquashfs of stock image failed (see $WORK/unsquash_stock.log)"

log "extracting tsunglung rootfs -> $WORK/ts_tree"
unsquashfs -d "$WORK/ts_tree" "$TS_IMG" >"$WORK/unsquash_ts.log" 2>&1 \
    || die "unsquashfs of tsunglung image failed (see $WORK/unsquash_ts.log)"

# -----------------------------------------------------------------------------
# 2. Integrity checks of the extracted trees
# -----------------------------------------------------------------------------
check_tree() {
    local tree=$1 label=$2
    [ -f "$tree/linuxrc" ]        || die "$label: /linuxrc missing"
    [ -L "$tree/sbin/init" ]      || die "$label: /sbin/init is not a symlink"
    [ "$(readlink "$tree/sbin/init")" = "../bin/busybox" ] || die "$label: /sbin/init -> $(readlink "$tree/sbin/init")"
    [ -c "$tree/dev/console" ]    || die "$label: /dev/console missing (mknod not possible => tree on DrvFs?)"
    [ "$(stat -c '%t:%T' "$tree/dev/console")" = "5:1" ] || die "$label: /dev/console is not 5:1"
    [ -f "$tree/bin/busybox" ]    || die "$label: /bin/busybox missing"
    [ "$(stat -c '%a' "$tree/bin/busybox")" = "4755" ] || die "$label: /bin/busybox not 4755 (setuid root lost)"
    [ "$(stat -c '%u:%g' "$tree/bin/busybox")" = "0:0" ] || die "$label: /bin/busybox not root:root"
    local n; n=$(find "$tree" | wc -l)
    [ "$n" -gt 2000 ] || die "$label: tree suspiciously small ($n entries)"
    log "$label OK: $n entries, /linuxrc, /sbin/init->busybox, /dev/console 5:1, busybox 4755 root"
}
check_tree "$WORK/stock_tree" "stock_tree"
check_tree "$WORK/ts_tree"    "ts_tree"
[ -x "$WORK/ts_tree/bin/telnetd" ] || die "ts_tree: /bin/telnetd missing"
log "tsunglung telnetd: $(stat -c '%a %u:%g %s' "$WORK/ts_tree/bin/telnetd") $(md5sum "$WORK/ts_tree/bin/telnetd" | cut -d' ' -f1)"

# -----------------------------------------------------------------------------
# 3. "Minimal" variant: stock 4.3.7 + static telnetd + tsunglung S60 hook
# -----------------------------------------------------------------------------
log "preparing minimal tree (stock 4.3.7 + telnetd + S60service hook)"
cp -a "$WORK/stock_tree" "$WORK/min_tree"
# cp -a (not install) keeps the original mode/uid/gid AND mtime, so the
# build stays byte-for-byte reproducible (together with -mkfs-time).
cp -a "$WORK/ts_tree/bin/telnetd" "$WORK/min_tree/bin/telnetd"
cp -a "$WORK/ts_tree/etc/init.d/S60service" "$WORK/min_tree/etc/init.d/S60service"
# cp updated the mtime of the 2 modified directories; restore it so the build
# stays reproducible (directory mtimes are stored in the squashfs).
touch -r "$WORK/stock_tree/bin"           "$WORK/min_tree/bin"
touch -r "$WORK/stock_tree/etc/init.d"    "$WORK/min_tree/etc/init.d"
# check that the hook contains both tsunglung additions
grep -q 'telnetd'            "$WORK/min_tree/etc/init.d/S60service" || die "minimal S60service: telnetd call missing"
grep -q 'post_init.sh'       "$WORK/min_tree/etc/init.d/S60service" || die "minimal S60service: post_init hook missing"
check_tree "$WORK/min_tree" "min_tree"

# -----------------------------------------------------------------------------
# 4. Build the squashfs images (same options as stock)
# -----------------------------------------------------------------------------
build_sq() {
    local tree=$1 out=$2 label=$3 mkfstime=$4
    log "mksquashfs $label -> $(basename "$out")"
    rm -f "$out"
    mksquashfs "$tree" "$out" "${MKSQ_OPTS[@]}" -mkfs-time "$mkfstime" -quiet \
        || die "mksquashfs failed for $label"
    local sz; sz=$(stat -c%s "$out")
    [ "$sz" -lt "$PART_SIZE" ] || die "$label: squashfs $sz bytes >= partition $PART_SIZE"
    local flags; flags=$(od -An -tx1 -j24 -N2 "$out" | tr -d ' \n')
    [ "$flags" = "c000" ] || die "$label: superblock flags=$flags, expected c000 (stock profile)"
    local magic; magic=$(head -c4 "$out")
    [ "$magic" = "$SQUASHFS_MAGIC" ] || die "$label: invalid squashfs magic"
    log "$label: $sz bytes (< $PART_SIZE), flags=$flags, magic=$magic"
    echo "-- $label: $sz bytes, flags=0x00c0" >> "$LOG"
    unsquashfs -s "$out" >> "$LOG" 2>&1
}

build_sq "$WORK/ts_tree"  "$HERE/custom_rootfs_v2.squashfs"     "v2 full tsunglung"        "$TS_TIME"
build_sq "$WORK/min_tree" "$HERE/custom_rootfs_v2_min.squashfs" "v2 minimal (stock 4.3.7)"    "$STOCK_TIME"

# -----------------------------------------------------------------------------
# 5. Pad to the exact partition size + md5
# -----------------------------------------------------------------------------
pad_and_md5() {
    local sq=$1 img=$2 label=$3
    cp -f "$sq" "$img"
    truncate -s "$PART_SIZE" "$img"
    local sz; sz=$(stat -c%s "$img")
    [ "$sz" -eq "$PART_SIZE" ] || die "$label: .img $sz != $PART_SIZE"
    ( cd "$(dirname "$img")" && md5sum "$(basename "$img")" > "$(basename "${img%.img}.md5")" )
    local m; m=$(cut -d' ' -f1 "$(dirname "$img")/$(basename "${img%.img}.md5")")
    log "$label: .img $sz bytes, md5 $m"
    echo "-- $label: .img $sz bytes, md5 $m" >> "$LOG"
}

pad_and_md5 "$HERE/custom_rootfs_v2.squashfs"     "$HERE/custom_rootfs_v2.img"     "v2 full tsunglung"
pad_and_md5 "$HERE/custom_rootfs_v2_min.squashfs" "$HERE/custom_rootfs_v2_min.img" "v2 minimal (stock 4.3.7)"

# Reference: original tsunglung image, NOT rebuilt, only padded to 0x750000.
# (byte-for-byte "known good" control in case a rebuilt image fails)
cp -f "$TS_IMG" "$HERE/custom_rootfs_v2_orig_tsunglung.img"
truncate -s "$PART_SIZE" "$HERE/custom_rootfs_v2_orig_tsunglung.img"
( cd "$HERE" && md5sum custom_rootfs_v2_orig_tsunglung.img > custom_rootfs_v2_orig_tsunglung.md5 )
log "padded original tsunglung reference: md5 $(cut -d' ' -f1 "$HERE/custom_rootfs_v2_orig_tsunglung.md5")"
echo "-- padded original tsunglung reference: md5 $(cut -d' ' -f1 "$HERE/custom_rootfs_v2_orig_tsunglung.md5")" >> "$LOG"

# -----------------------------------------------------------------------------
# 6. Verify the produced images by re-extracting them
# -----------------------------------------------------------------------------
verify_image() {
    local img=$1 tree=$2 label=$3 dir
    dir="$WORK/verify_${label//[^a-zA-Z0-9]/_}"
    rm -rf "$dir"
    unsquashfs -d "$dir" "$img" >/dev/null 2>&1 || die "$label: re-extraction failed (invalid image)"
    [ -c "$dir/dev/console" ] || die "$label: /dev/console missing in the image"
    [ "$(stat -c '%t:%T' "$dir/dev/console")" = "5:1" ] || die "$label: /dev/console != 5:1"
    [ "$(stat -c '%a' "$dir/bin/busybox")" = "4755" ] || die "$label: busybox not 4755"
    [ -f "$dir/linuxrc" ] || die "$label: /linuxrc missing"
    [ -L "$dir/sbin/init" ] || die "$label: /sbin/init missing"
    [ -f "$dir/bin/telnetd" ] || die "$label: /bin/telnetd missing"
    grep -q 'telnetd' "$dir/etc/init.d/S60service" || die "$label: telnetd hook missing"
    # content comparison: diff compares files/dirs/links but NOT device nodes
    # ("is a character special file while ..."); those lines are ignored
    # (device nodes are checked explicitly above).
    local d bad
    d=$(diff -r --no-dereference "$tree" "$dir" 2>&1 || true)
    bad=$(printf '%s\n' "$d" | grep -v -E '^File .* is a (character|block) special file while file .* is a (character|block) special file$' | sed '/^$/d' || true)
    [ -z "$bad" ] || { printf '%s\n' "$bad" >"$WORK/verify_${label//[^a-zA-Z0-9]/_}.diff"; die "$label: image differs from the source tree (see $WORK/verify_*.diff)"; }
    if [ "$(find "$tree" -type c | wc -l)" != "$(find "$dir" -type c | wc -l)" ]; then
        die "$label: character device node count differs"
    fi
    local n; n=$(find "$dir" | wc -l)
    log "$label: re-extraction OK ($n entries, identical to source tree, console 5:1, busybox 4755)"
    {
        echo "-- verification $label: OK ($n entries)"
        echo "   /linuxrc            : $(head -c 40 "$dir/linuxrc" | tr '\n' ' ')"
        echo "   /sbin/init          : -> $(readlink "$dir/sbin/init")"
        echo "   /dev/console        : $(stat -c '%F %a %u:%g 5:1' "$dir/dev/console")"
        echo "   /bin/busybox        : $(stat -c '%a %u:%g' "$dir/bin/busybox")"
        echo "   /bin/telnetd        : $(stat -c '%a %u:%g %s' "$dir/bin/telnetd")"
        echo "   S60service          : $(grep -c telnetd "$dir/etc/init.d/S60service") x telnetd, post_init=$(grep -c post_init.sh "$dir/etc/init.d/S60service")"
        echo "   non-root ids        : $(find "$dir" ! -uid 0 -printf '%u:%g %p\n' | head -5 | tr '\n' ' ')"
        echo "   unsquashfs -l lines : $(unsquashfs -l "$img" 2>/dev/null | wc -l) lines"
    } >> "$LOG"
}

verify_image "$HERE/custom_rootfs_v2.squashfs"     "$WORK/ts_tree"  "v2_tsunglung"
verify_image "$HERE/custom_rootfs_v2_min.squashfs" "$WORK/min_tree" "v2_minimal"

# -----------------------------------------------------------------------------
# 7. Summary
# -----------------------------------------------------------------------------
{
    echo "=================================================================="
    echo " SUMMARY ($(date -u '+%Y-%m-%d %H:%M:%SZ'))"
    for f in custom_rootfs_v2 custom_rootfs_v2_min custom_rootfs_v2_orig_tsunglung; do
        [ -f "$HERE/$f.img" ] && printf ' %-40s %9s B  md5=%s\n' "$f.img" "$(stat -c%s "$HERE/$f.img")" "$(cut -d' ' -f1 "$HERE/$f.md5")"
    done
    echo " mtd2 partition limit = $PART_SIZE bytes (0x750000)"
    echo "=================================================================="
} | tee -a "$LOG"

log "done. See $LOG"
