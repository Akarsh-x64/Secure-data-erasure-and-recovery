#pragma once
#pragma pack(push, 1)
#include <cstdint>

namespace Erasure {
namespace FileSystems {
    struct Ext3SuperBlock {
        uint32_t inode_count;         // 0x00
        uint32_t blocks_count;        // 0x04
        uint8_t  pad1[16];            // 0x08 - 0x17
        uint32_t log_block_size;      // 0x18
        uint8_t  pad2[4];             // 0x1C - 0x1F
        uint32_t s_block_per_group;   // 0x20
        uint8_t  pad3[4];             // 0x24 - 0x27
        uint32_t s_inodes_per_group;  // 0x28
        uint8_t  pad4[12];            // 0x2C - 0x37
        uint16_t s_magic;             // 0x38 (56 bytes offset)
        uint8_t  pad5[18];            // 0x3A - 0x4B
        uint32_t s_rev_level;         // 0x4C
        uint8_t  pad6[8];             // 0x50 - 0x57
        uint16_t inode_size;          // 0x58
        uint8_t  pad7[934];           // Pad to full 1024-byte block
    };

    struct Ext3GroupDescriptor {
        uint32_t bg_block_bitmap;      
        uint32_t bg_inode_bitmap;      
        uint32_t bg_inode_table;       
        uint16_t bg_free_blocks_count; 
        uint16_t bg_free_inodes_count; 
        uint16_t bg_used_dirs_count;    
        uint16_t bg_pad;              
        uint32_t bg_reserved[3];       
    };

    struct Ext3Inode {
        uint16_t i_mode;              
        uint16_t i_uid;                
        uint32_t i_size;              
        uint32_t i_atime;             
        uint32_t i_ctime;              
        uint32_t i_mtime;              
        uint32_t i_dtime;              
        uint16_t i_gid;               
        uint16_t i_links_count;        
        uint32_t i_blocks;             
        uint32_t i_flags;              
        uint32_t l_i_reserved1;        
        uint32_t i_block[15];          
        uint32_t i_generation;         
        uint32_t i_file_acl;           
        uint32_t i_dir_acl;            
        uint32_t i_faddr;              
        uint8_t  l_i_reserved2[12];    
    };
    struct Ext3DirEntry {
        uint32_t inode;       
        uint16_t rec_len;     
        uint8_t  name_len;    
        uint8_t  file_type;   
        char name[];      
    };
}
}
#pragma pack(pop)



