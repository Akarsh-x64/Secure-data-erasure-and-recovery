#include "SignatureCarver.h"
#include <cstring>
#include <algorithm>

namespace Erasure {
namespace Verification {

SignatureCarver::SignatureCarver() {
    InitializeSignatures();
}

void SignatureCarver::InitializeSignatures() {
    m_signatures.clear();

    // Helper lambda to register signatures
    auto Add = [this](const std::string& name, const std::string& ext, SignatureCategory cat,
                      const std::vector<uint8_t>& bytes, uint32_t offset = 0) {
        m_signatures.push_back({ name, ext, cat, bytes, offset });
    };

    // =========================================================================
    // 1. Documents & Office Files (15 signatures)
    // =========================================================================
    Add("PDF Document", "pdf", SignatureCategory::DOCUMENT, { 0x25, 0x50, 0x44, 0x46, 0x2D }); // %PDF-
    Add("MS Office (OLE2 / DOC / XLS / PPT)", "doc", SignatureCategory::DOCUMENT, { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 });
    Add("Office OpenXML / ZIP (DOCX / XLSX / PPTX)", "docx", SignatureCategory::DOCUMENT, { 0x50, 0x4B, 0x03, 0x04 }); // PK..
    Add("Rich Text Format", "rtf", SignatureCategory::DOCUMENT, { 0x7B, 0x5C, 0x72, 0x74, 0x66 }); // {\rtf
    Add("PostScript Document", "ps", SignatureCategory::DOCUMENT, { 0x25, 0x21, 0x50, 0x53 }); // %!PS
    Add("Encapsulated PostScript", "eps", SignatureCategory::DOCUMENT, { 0xC5, 0xD0, 0xD3, 0xC6 });
    Add("OpenDocument Format (ODT / ODS / ODP)", "odt", SignatureCategory::DOCUMENT, { 0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x06, 0x00 });
    Add("EPUB E-Book", "epub", SignatureCategory::DOCUMENT, { 0x50, 0x4B, 0x03, 0x04, 0x0A, 0x00, 0x02, 0x00 });
    Add("WordPerfect Document", "wpd", SignatureCategory::DOCUMENT, { 0xFF, 0x57, 0x50, 0x43 });
    Add("Compiled HTML Help", "chm", SignatureCategory::DOCUMENT, { 0x49, 0x54, 0x53, 0x46 }); // ITSF
    Add("DJVU Document", "djvu", SignatureCategory::DOCUMENT, { 0x41, 0x54, 0x26, 0x54, 0x46, 0x4F, 0x52, 0x4D }); // AT&TFORM
    Add("LaTeX / TeX DVI", "dvi", SignatureCategory::DOCUMENT, { 0xF7, 0x02 });
    Add("AbiWord Document", "abw", SignatureCategory::DOCUMENT, { 0x3C, 0x3F, 0x78, 0x6D, 0x6C }); // <?xml
    Add("FictionBook E-Book", "fb2", SignatureCategory::DOCUMENT, { 0x3C, 0x3F, 0x78, 0x6D, 0x6C, 0x20, 0x76 });
    Add("BibTeX Database", "bib", SignatureCategory::DOCUMENT, { 0x40, 0x61, 0x72, 0x74, 0x69, 0x63, 0x6C, 0x65 }); // @article

    // =========================================================================
    // 2. Images & Photos (20 signatures)
    // =========================================================================
    Add("JPEG / JFIF Image", "jpg", SignatureCategory::IMAGE, { 0xFF, 0xD8, 0xFF, 0xE0 });
    Add("JPEG / Exif Image", "jpg", SignatureCategory::IMAGE, { 0xFF, 0xD8, 0xFF, 0xE1 });
    Add("JPEG / SPIFF Image", "jpg", SignatureCategory::IMAGE, { 0xFF, 0xD8, 0xFF, 0xE8 });
    Add("JPEG Raw Image", "jpg", SignatureCategory::IMAGE, { 0xFF, 0xD8, 0xFF, 0xDB });
    Add("JPEG / Adobe Image", "jpg", SignatureCategory::IMAGE, { 0xFF, 0xD8, 0xFF, 0xEE });
    Add("PNG Image", "png", SignatureCategory::IMAGE, { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A });
    Add("GIF Image (87a)", "gif", SignatureCategory::IMAGE, { 0x47, 0x49, 0x46, 0x38, 0x37, 0x61 }); // GIF87a
    Add("GIF Image (89a)", "gif", SignatureCategory::IMAGE, { 0x47, 0x49, 0x46, 0x38, 0x39, 0x61 }); // GIF89a
    Add("Windows Bitmap", "bmp", SignatureCategory::IMAGE, { 0x42, 0x4D }); // BM
    Add("TIFF Image (Little Endian)", "tif", SignatureCategory::IMAGE, { 0x49, 0x49, 0x2A, 0x00 }); // II*.
    Add("TIFF Image (Big Endian)", "tif", SignatureCategory::IMAGE, { 0x4D, 0x4D, 0x00, 0x2A }); // MM.*
    Add("Photoshop Document", "psd", SignatureCategory::IMAGE, { 0x38, 0x42, 0x50, 0x53 }); // 8BPS
    Add("Windows Icon", "ico", SignatureCategory::IMAGE, { 0x00, 0x00, 0x01, 0x00 });
    Add("Windows Cursor", "cur", SignatureCategory::IMAGE, { 0x00, 0x00, 0x02, 0x00 });
    Add("Canon RAW Image (CR2)", "cr2", SignatureCategory::IMAGE, { 0x49, 0x49, 0x2A, 0x00, 0x10, 0x00, 0x00, 0x00, 0x43, 0x52 });
    Add("Nikon RAW Image (NEF)", "nef", SignatureCategory::IMAGE, { 0x4D, 0x4D, 0x00, 0x2A });
    Add("High Efficiency Image (HEIC)", "heic", SignatureCategory::IMAGE, { 0x66, 0x74, 0x79, 0x70, 0x68, 0x65, 0x69, 0x63 }, 4); // ftypheic at off 4
    Add("High Efficiency Image (HEIF)", "heif", SignatureCategory::IMAGE, { 0x66, 0x74, 0x79, 0x70, 0x6D, 0x69, 0x66, 0x31 }, 4);
    Add("WebP Image", "webp", SignatureCategory::IMAGE, { 0x52, 0x49, 0x46, 0x46 }); // RIFF (check WEBP at 8)
    Add("Radiance High Dynamic Range", "hdr", SignatureCategory::IMAGE, { 0x23, 0x3F, 0x52, 0x41, 0x44, 0x49, 0x41, 0x4E, 0x43, 0x45 }); // #?RADIANCE

    // =========================================================================
    // 3. Archives & Compression (22 signatures)
    // =========================================================================
    Add("7-Zip Compressed Archive", "7z", SignatureCategory::ARCHIVE, { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C });
    Add("RAR Archive (v4)", "rar", SignatureCategory::ARCHIVE, { 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x00 }); // Rar!...
    Add("RAR Archive (v5)", "rar", SignatureCategory::ARCHIVE, { 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07, 0x01, 0x00 });
    Add("GZIP Compressed Archive", "gz", SignatureCategory::ARCHIVE, { 0x1F, 0x8B });
    Add("BZIP2 Compressed Archive", "bz2", SignatureCategory::ARCHIVE, { 0x42, 0x5A, 0x68 }); // BZh
    Add("XZ Compressed Archive", "xz", SignatureCategory::ARCHIVE, { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 }); // .7zXZ.
    Add("TAR Archive", "tar", SignatureCategory::ARCHIVE, { 0x75, 0x73, 0x74, 0x61, 0x72 }, 257); // ustar at off 257
    Add("Zstandard Compressed Archive", "zst", SignatureCategory::ARCHIVE, { 0x28, 0xB5, 0x2F, 0xFD });
    Add("LZ4 Compressed Archive", "lz4", SignatureCategory::ARCHIVE, { 0x04, 0x22, 0x4D, 0x18 });
    Add("Cabinet Archive (CAB)", "cab", SignatureCategory::ARCHIVE, { 0x4D, 0x53, 0x43, 0x46 }); // MSCF
    Add("Debian Package", "deb", SignatureCategory::ARCHIVE, { 0x21, 0x3C, 0x61, 0x72, 0x63, 0x68, 0x3E, 0x0A }); // !<arch>\n
    Add("RedHat Package (RPM)", "rpm", SignatureCategory::ARCHIVE, { 0xED, 0xAB, 0xEE, 0xDB });
    Add("AR Archive", "a", SignatureCategory::ARCHIVE, { 0x21, 0x3C, 0x61, 0x72, 0x63, 0x68, 0x3E });
    Add("LHA / LZH Archive", "lzh", SignatureCategory::ARCHIVE, { 0x2D, 0x6C, 0x68 }, 2); // -lh at off 2
    Add("StuffIt Archive", "sit", SignatureCategory::ARCHIVE, { 0x53, 0x74, 0x75, 0x66, 0x66, 0x49, 0x74 }); // StuffIt
    Add("ISO 9660 CD/DVD Image", "iso", SignatureCategory::ARCHIVE, { 0x43, 0x44, 0x30, 0x30, 0x31 }, 0x8001); // CD001
    Add("WIM Imaging Archive", "wim", SignatureCategory::ARCHIVE, { 0x4D, 0x53, 0x57, 0x49, 0x4D, 0x00, 0x00, 0x00 }); // MSWIM...
    Add("CPIO Archive", "cpio", SignatureCategory::ARCHIVE, { 0x30, 0x37, 0x30, 0x37, 0x30, 0x31 }); // 070701
    Add("Brotli Compressed Data", "br", SignatureCategory::ARCHIVE, { 0xCE, 0xB2, 0xCF, 0x81 });
    Add("Snappy Framed Archive", "sz", SignatureCategory::ARCHIVE, { 0xFF, 0x06, 0x00, 0x00, 0x73, 0x4E, 0x61, 0x50, 0x70, 0x59 }); // ..sNaPpY
    Add("SquashFS Filesystem", "squashfs", SignatureCategory::ARCHIVE, { 0x68, 0x73, 0x71, 0x73 }); // hsqs
    Add("Android Package (APK)", "apk", SignatureCategory::ARCHIVE, { 0x50, 0x4B, 0x03, 0x04 });

    // =========================================================================
    // 4. Audio & Video (20 signatures)
    // =========================================================================
    Add("MP3 Audio (ID3v2 Header)", "mp3", SignatureCategory::AUDIO_VIDEO, { 0x49, 0x44, 0x33 }); // ID3
    Add("MP3 Audio (Frame Sync 0xFFFB)", "mp3", SignatureCategory::AUDIO_VIDEO, { 0xFF, 0xFB });
    Add("MP3 Audio (Frame Sync 0xFFF3)", "mp3", SignatureCategory::AUDIO_VIDEO, { 0xFF, 0xF3 });
    Add("FLAC Audio", "flac", SignatureCategory::AUDIO_VIDEO, { 0x66, 0x4C, 0x61, 0x43 }); // fLaC
    Add("WAV Audio (RIFF)", "wav", SignatureCategory::AUDIO_VIDEO, { 0x52, 0x49, 0x46, 0x46 }); // RIFF
    Add("OGG Audio/Video Container", "ogg", SignatureCategory::AUDIO_VIDEO, { 0x4F, 0x67, 0x67, 0x53 }); // OggS
    Add("MP4 / QuickTime Video (ftyp)", "mp4", SignatureCategory::AUDIO_VIDEO, { 0x66, 0x74, 0x79, 0x70 }, 4); // ftyp at off 4
    Add("AVI Video Container", "avi", SignatureCategory::AUDIO_VIDEO, { 0x52, 0x49, 0x46, 0x46 }); // RIFF
    Add("Matroska / WebM Container (EBML)", "mkv", SignatureCategory::AUDIO_VIDEO, { 0x1A, 0x45, 0xDF, 0xA3 });
    Add("Windows Media Video (ASF / WMV / WMA)", "wmv", SignatureCategory::AUDIO_VIDEO, { 0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11 });
    Add("MPEG-PS Video", "mpg", SignatureCategory::AUDIO_VIDEO, { 0x00, 0x00, 0x01, 0xBA });
    Add("MPEG-TS Transport Stream", "ts", SignatureCategory::AUDIO_VIDEO, { 0x47 }); // Sync byte 0x47
    Add("Flash Video (FLV)", "flv", SignatureCategory::AUDIO_VIDEO, { 0x46, 0x4C, 0x56, 0x01 }); // FLV.
    Add("MIDI Audio File", "mid", SignatureCategory::AUDIO_VIDEO, { 0x4D, 0x54, 0x68, 0x64 }); // MThd
    Add("Advanced Audio Coding (AAC ADTS)", "aac", SignatureCategory::AUDIO_VIDEO, { 0xFF, 0xF1 });
    Add("AIFF Audio File", "aif", SignatureCategory::AUDIO_VIDEO, { 0x46, 0x4F, 0x52, 0x4D }); // FORM
    Add("RealMedia Stream (RM)", "rm", SignatureCategory::AUDIO_VIDEO, { 0x2E, 0x52, 0x4D, 0x46 }); // .RMF
    Add("WebM Video Container", "webm", SignatureCategory::AUDIO_VIDEO, { 0x1A, 0x45, 0xDF, 0xA3 });
    Add("QuickTime Movie", "mov", SignatureCategory::AUDIO_VIDEO, { 0x6D, 0x6F, 0x6F, 0x76 }, 4); // moov at off 4
    Add("3GPP Multimedia Container", "3gp", SignatureCategory::AUDIO_VIDEO, { 0x66, 0x74, 0x79, 0x70, 0x33, 0x67 }, 4);

    // =========================================================================
    // 5. Executables & Binaries (18 signatures)
    // =========================================================================
    Add("Windows PE / DOS Executable", "exe", SignatureCategory::EXECUTABLE, { 0x4D, 0x5A }); // MZ
    Add("Linux ELF Executable / Shared Object", "elf", SignatureCategory::EXECUTABLE, { 0x7F, 0x45, 0x4C, 0x46 }); // \x7FELF
    Add("Mach-O Binary (32-bit LE)", "macho", SignatureCategory::EXECUTABLE, { 0xCE, 0xFA, 0xED, 0xFE });
    Add("Mach-O Binary (64-bit LE)", "macho", SignatureCategory::EXECUTABLE, { 0xCF, 0xFA, 0xED, 0xFE });
    Add("Mach-O Binary (32-bit BE)", "macho", SignatureCategory::EXECUTABLE, { 0xFE, 0xED, 0xFA, 0xCE });
    Add("Mach-O Binary (64-bit BE)", "macho", SignatureCategory::EXECUTABLE, { 0xFE, 0xED, 0xFA, 0xCF });
    Add("Mach-O Universal Binary", "macho", SignatureCategory::EXECUTABLE, { 0xCA, 0xFE, 0xBA, 0xBE });
    Add("Java Class Bytecode", "class", SignatureCategory::EXECUTABLE, { 0xCA, 0xFE, 0xBA, 0xBE });
    Add("WebAssembly Binary", "wasm", SignatureCategory::EXECUTABLE, { 0x00, 0x61, 0x73, 0x6D }); // \0asm
    Add("Dalvik Executable (DEX)", "dex", SignatureCategory::EXECUTABLE, { 0x64, 0x65, 0x78, 0x0A }); // dex\n
    Add("Compiled Python Bytecode (pyc)", "pyc", SignatureCategory::EXECUTABLE, { 0x0D, 0x0D, 0x0A });
    Add("Windows Installer Package (MSI)", "msi", SignatureCategory::EXECUTABLE, { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 });
    Add("Windows Minidump (DMP)", "dmp", SignatureCategory::EXECUTABLE, { 0x4D, 0x44, 0x4D, 0x50 }); // MDMP
    Add("Linux Core Dump", "core", SignatureCategory::EXECUTABLE, { 0x7F, 0x45, 0x4C, 0x46, 0x02 });
    Add("Commodore 64 Executable", "prg", SignatureCategory::EXECUTABLE, { 0x01, 0x08 });
    Add("GameBoy ROM", "gb", SignatureCategory::EXECUTABLE, { 0xCE, 0xED, 0x66, 0x66 }, 0x104);
    Add("Nintendo DS ROM", "nds", SignatureCategory::EXECUTABLE, { 0x24, 0xFF, 0xAE, 0x51 }, 0xC0);
    Add("Shell Script / Shebang", "sh", SignatureCategory::EXECUTABLE, { 0x23, 0x21 }); // #!

    // =========================================================================
    // 6. Databases, Virtual Disks & Systems (25 signatures)
    // =========================================================================
    Add("SQLite 3 Database", "sqlite", SignatureCategory::DATABASE_SYSTEM, { 0x53, 0x51, 0x4C, 0x69, 0x74, 0x65, 0x20, 0x66, 0x6F, 0x72, 0x6D, 0x61, 0x74, 0x20, 0x33, 0x00 });
    Add("Wireshark PCAP Capture (LE)", "pcap", SignatureCategory::DATABASE_SYSTEM, { 0xD4, 0xC3, 0xB2, 0xA1 });
    Add("Wireshark PCAP Capture (BE)", "pcap", SignatureCategory::DATABASE_SYSTEM, { 0xA1, 0xB2, 0xC3, 0xD4 });
    Add("Wireshark PCAPNG Capture", "pcapng", SignatureCategory::DATABASE_SYSTEM, { 0x0A, 0x0D, 0x0D, 0x0A });
    Add("VMware Virtual Disk (VMDK)", "vmdk", SignatureCategory::DATABASE_SYSTEM, { 0x4B, 0x44, 0x4D, 0x56 }); // KDMV
    Add("Virtual Hard Disk (VHD)", "vhd", SignatureCategory::DATABASE_SYSTEM, { 0x63, 0x6F, 0x6E, 0x65, 0x63, 0x74, 0x69, 0x78 }); // conectix
    Add("Hyper-V Virtual Disk (VHDX)", "vhdx", SignatureCategory::DATABASE_SYSTEM, { 0x76, 0x68, 0x64, 0x78, 0x66, 0x69, 0x6C, 0x65 }); // vhdxfile
    Add("VirtualBox Disk Image (VDI)", "vdi", SignatureCategory::DATABASE_SYSTEM, { 0x3C, 0x3C, 0x3C, 0x20, 0x4F, 0x72, 0x61, 0x63, 0x6C, 0x65 }); // <<< Oracle
    Add("QEMU Copy-On-Write (QCOW2)", "qcow2", SignatureCategory::DATABASE_SYSTEM, { 0x51, 0x46, 0x49, 0xFB }); // QFI\xFB
    Add("Windows Registry Hive", "dat", SignatureCategory::DATABASE_SYSTEM, { 0x72, 0x65, 0x67, 0x66 }); // regf
    Add("Windows Event Log (EVTX)", "evtx", SignatureCategory::DATABASE_SYSTEM, { 0x45, 0x6C, 0x66, 0x46, 0x69, 0x6C, 0x65, 0x00 }); // ElfFile\0
    Add("Windows Prefetch File (MAM)", "pf", SignatureCategory::DATABASE_SYSTEM, { 0x4D, 0x41, 0x4D, 0x04 }); // MAM.
    Add("Berkeley DB Database", "db", SignatureCategory::DATABASE_SYSTEM, { 0x00, 0x05, 0x31, 0x62 });
    Add("PostgreSQL WAL File", "wal", SignatureCategory::DATABASE_SYSTEM, { 0xD0, 0x00 });
    Add("MySQL InnoDB Tablespace", "ibd", SignatureCategory::DATABASE_SYSTEM, { 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 });
    Add("Microsoft Outlook PST Data", "pst", SignatureCategory::DATABASE_SYSTEM, { 0x21, 0x42, 0x44, 0x4E }); // !BDN
    Add("Microsoft Access Database (MDB)", "mdb", SignatureCategory::DATABASE_SYSTEM, { 0x00, 0x01, 0x00, 0x00, 0x53, 0x74, 0x61, 0x6E, 0x64, 0x61, 0x72, 0x64, 0x20, 0x4A, 0x65, 0x74 });
    Add("Apple HFS+ Volume Header", "hfs", SignatureCategory::DATABASE_SYSTEM, { 0x48, 0x2B, 0x00, 0x04 }, 0x400); // H+ at off 1024
    Add("Ext2/3/4 Superblock Signature", "ext4", SignatureCategory::DATABASE_SYSTEM, { 0x53, 0xEF }, 0x438); // 0xEF53 at off 1080
    Add("XFS Superblock Signature", "xfs", SignatureCategory::DATABASE_SYSTEM, { 0x58, 0x46, 0x53, 0x42 }); // XFSB
    Add("NTFS Volume Boot Record", "ntfs", SignatureCategory::DATABASE_SYSTEM, { 0x4E, 0x54, 0x46, 0x53, 0x20, 0x20, 0x20, 0x20 }, 3); // "NTFS    "
    Add("exFAT Volume Boot Record", "exfat", SignatureCategory::DATABASE_SYSTEM, { 0x45, 0x58, 0x46, 0x41, 0x54, 0x20, 0x20, 0x20 }, 3); // "EXFAT   "
    Add("TrueCrypt / VeraCrypt Volume", "tc", SignatureCategory::DATABASE_SYSTEM, { 0x54, 0x52, 0x55, 0x45 }); // TRUE
    Add("LUKS Encrypted Partition", "luks", SignatureCategory::DATABASE_SYSTEM, { 0x4C, 0x55, 0x4B, 0x53, 0xBA, 0xBE }); // LUKS\xBA\xBE
    Add("BitLocker Encrypted Volume", "fve", SignatureCategory::DATABASE_SYSTEM, { 0x2D, 0x46, 0x56, 0x45, 0x2D, 0x46, 0x53, 0x2D }); // -FVE-FS-
}

std::vector<CarvedArtifact> SignatureCarver::ScanBuffer(const uint8_t* buffer, size_t size, uint64_t basePhysicalOffset, uint32_t sectorSize) const {
    std::vector<CarvedArtifact> detections;
    if (!buffer || size < 4) return detections;
    if (sectorSize == 0) sectorSize = 512;

    // We scan on 512-byte sector boundaries (where files begin on disk) as well as any arbitrary offset
    // To balance speed and completeness, inspect sector boundaries first, and then sliding windows
    for (size_t offset = 0; offset < size; ++offset) {
        // Fast skip if byte is zero or 0xFF (no magic header begins with 0x00 or 0xFF except JPEG)
        uint8_t firstByte = buffer[offset];
        if (firstByte == 0x00) {
            continue;
        }

        for (const auto& sig : m_signatures) {
            size_t sigLen = sig.headerBytes.size();
            size_t targetPos = offset + sig.headerOffset;

            if (targetPos + sigLen <= size) {
                if (std::memcmp(buffer + targetPos, sig.headerBytes.data(), sigLen) == 0) {
                    uint64_t diskOffset = basePhysicalOffset + targetPos;
                    uint64_t lba = diskOffset / sectorSize;

                    detections.push_back({
                        sig.name,
                        sig.extension,
                        sig.category,
                        diskOffset,
                        lba
                    });
                }
            }
        }
    }

    return detections;
}

} // namespace Verification
} // namespace Erasure
