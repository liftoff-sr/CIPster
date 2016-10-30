/*
 *
 *  Created on: Apr 17, 2015
 *      Author: Aleksey Timin
 *      (c) BPA, Ltd
 */

#ifndef INC_CIPS_CIP_FILE_H_
#define INC_CIPS_CIP_FILE_H_

#include <stdio.h>
#include <vector>

#include "cip_objects/cip_string_int.h"
#include "cipster_api.h"

// * Settings
#define CIP_FILE_INCREMENTAL_BURN_NUMBER    100
#define CIP_FILE_INCREMENTAL_BURN_TIME      0
#define CIP_FILE_MAX_TRANSFER_SIZE          50

#define CIP_FILE_CLASS_CODE                 0x37
#define CIP_FILE_MAX_NUM_INSTANCES          16

// * Attributes
#define CIP_FILE_ATTR_DIRECTORY             32
#define CIP_FILE_ATTR_STATE                 1
#define CIP_FILE_ATTR_INSTANCE_NAME         2
#define CIP_FILE_ATTR_INST_FORMAT_V         3
#define CIP_FILE_ATTR_FILE_NAME             4
#define CIP_FILE_ATTR_FILE_REV              5
#define CIP_FILE_ATTR_FILE_SIZE             6
#define CIP_FILE_ATTR_FILE_CHECKSUM         7
#define CIP_FILE_ATTR_INVOC_METHOD          8
#define CIP_FILE_ATTR_FILE_SAVE_PRMS        9
#define CIP_FILE_ATTR_FILE_TYPE             10

// * Instance services
#define CIP_FILE_SERV_INIT_UPLOAD           0x4B
#define CIP_FILE_SERV_UPLOAD_TRANSFER       0x4F
#define CIP_FILE_SERV_INIT_DOWNLOAD         0x4C
#define CIP_FILE_SERV_DOWNPLOAD_TRANSFER    0x50


// * States of the file object
#define CIP_FILE_STATE_NONEXIT              0
#define CIP_FILE_STATE_EMPTY                1
#define CIP_FILE_STATE_LOADED               2
#define CIP_FILE_STATE_UPLOAD_INIT          3
#define CIP_FILE_STATE_DOWNLOAD_INIT        4
#define CIP_FILE_STATE_UPLOAD               5
#define CIP_FILE_STATE_DOWNLOAD             6
#define CIP_FILE_STATE_STORING              7

// * Invocation methods of the file object
#define CIP_FILE_INVOC_NO_ACTION            0
#define CIP_FILE_INVOC_RESET                1
#define CIP_FILE_INVOC_PWR_CYCLE            2
#define CIP_FILE_INVOC_START_SERV           3

// * File types of the file object
#define CIP_FILE_FILE_TYPE_RW               0
#define CIP_FILE_FILE_TYPE_R                1

// * Transfer Packet Type
#define CIP_FILE_FIRST_TRANSFER_PACKET      0
#define CIP_FILE_MIDDLE_TRANSFER_PACKET     1
#define CIP_FILE_LAST_TRANSFER_PACKET       2
#define CIP_FILE_ABORT_TRANSFER_PACKET      3
#define CIP_FILE_FL_TRANSFER_PACKET         4

// * Error codes for Transfer Services
#define CIP_FILE_ERR_TRANSFER_OUT_OF_SEQ    0x06

// * Transfer's data
struct cip_file_transfer
{
    EipUint8    transfer_size;
    EipUint32   transfer_num;
    EipUint16   checksum;
    EipUint32   nbyte;
    EipUint16   format_version;

    S_CIP_Revision file_revision;
    char*       file_name;
};


// * Runtime data
struct CipFileInstance : public CipInstance
{
    EipUint8 state;
    S_CIP_String_Int* instance_name;
    S_CIP_String_Int* file_name;
    EipUint16 format_version;
    S_CIP_Revision rev;
    EipUint32 file_size;
    EipUint16 checksum;
    EipUint8 invoc_method;
    EipByte file_save_params;
    EipUint8 file_type;
    cip_file_transfer* transfer;
};



struct FileDirectory
{
    CipUint             instance_id;
    S_CIP_String_Int    instance_name;
    S_CIP_String_Int    file_name;
};


class CipFileDirAttribute : public CipAttribute
{
public:
    typedef std::vector<FileDirector*>  directory;

    CipFileDirAttribute(
            EipUint16 aAttributeId = 0,
            EipUint8 aType = 0,
            EipUint8 aFlags = 0,
            ) :
        CipAttribute(
            aAttributeId,
            aType,
            aFlags
            )
    {
    }

    ~CipFileDirAttribute()
    {
        while( dir.size() )
        {
            delete *dir.begin();
            dir.erase( dir.begin() );
        }
    }


protected:
    directory           dir;
};



/// Calculate checksum. See spec. 5-42.6.1
void CipFileCalcChecksum( EipUint16* sum, EipUint8 byte );


/*! Setup the Cip File class
 *
 * Create the Cip File class with zero instances
 *
 */
EipStatus CipFileClassCreate();


/*! /brief create an instance of the Object File class
 *
 * @param instance_id the ID of the new instance
 *
 */
CipFileInstance* CipFileInstanceCreate( EipUint32 instance_id,
        const char* instance_name,
        const char* name );

#endif    // INC_CIPS_CIP_FILE_H_
