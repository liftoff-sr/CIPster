/*
 * cip_file_object.c
 *
 *  Created on: Apr 17, 2015
 *      Author: Aleksey Timin
 *		(c) BPA, Ltd
 */

#include <string.h>


#include "cip_objects/cip_file.h"
#include "cipster_api.h"
#include "trace.h"


EipStatus  initiate_upload( CipInstance*, S_CIP_MR_Request*, S_CIP_MR_Response* );
EipStatus  upload_transfer( CipInstance*, S_CIP_MR_Request*, S_CIP_MR_Response* );
EipStatus  initiate_download( CipInstance*, S_CIP_MR_Request*, S_CIP_MR_Response* );
EipStatus  download_transfer( CipInstance*, S_CIP_MR_Request*, S_CIP_MR_Response* );


char* get_file_name( CipInstance* instance );

// A special callback to encode dir attribute
EipStatus getAttributeSingleFileObject( CipInstance* instance,
        S_CIP_MR_Request* req, S_CIP_MR_Response* resp );


EipStatus CipFileClassCreate()
{
    CipClass* clazz = CreateCipClass( CIP_FILE_CLASS_CODE,
            0xffffffff,     // class getAttributeAll mask
            0xffffffff,     // instance getAttributeAll mask
            0,              // no. instances
            "file object",
            1
            );

    clazz->AttributeInsert( CIP_FILE_ATTR_DIRECTORY, CIP_STRINGI, (void*) files, CIP_ATTRIB_GETABLE );

    clazz->AttributeInsert(  insertService( file_object_class->m_stSuper.pstClass,
            CIP_GET_ATTRIBUTE_SINGLE,
            &getAttributeSingleFileObject,
            "Get_Attribute_Single" );

    return kEipStatusOk;
}


CipInstance* cip_file_object_create_instance( EipUint32 instance_id,
        const char* instance_name,
        const char* name )
{
    CipInstance* file_object_instance = NULL;
    S_CIP_Class* file_object_class = getCIPClass( CIP_FILE_CLASS_CODE );

    if( file_object_class != NULL )
    {
        // Check if the instance already exists
        file_object_instance = getCIPInstance( file_object_class, instance_id );

        if( file_object_instance != NULL )
            return file_object_instance;

        EipUint16 count = *(EipUint16*) getAttribute( (CipInstance*) file_object_class,
                CIP_STD_ATTR_NUMBER_OF_INSTANCE )->pt2data;

        if( count < CIP_FILE_MAX_NUM_INSTANCES )
        {
            S_CIP_dir_file_struct* dir = NULL;
            file_object_instance = addCIPInstance( file_object_class, instance_id );

            // Registration the instance in directory
            dir = (S_CIP_dir_file_struct*) getAttribute( (CipInstance*) file_object_class,
                    CIP_FILE_ATTR_DIRECTORY )->pt2data;

            dir[count].instance_number = instance_id;
            dir[count].instance_name = cip_string_int_create_eng( instance_name );
            dir[count].file_name = cip_string_int_create_eng( name );

            // Initialize the attributes of the instance
            file_object_instance->rtData = IApp_CipCalloc( 1, sizeof(cip_file_object_struct) );
            cip_file_object_struct* rt_fo = file_object_instance->rtData;
            rt_fo->transfer = IApp_CipCalloc( 1, sizeof(cip_file_transfer) );

            // State
            rt_fo->state = CIP_FILE_STATE_NONEXIT;
            insertAttribute( file_object_instance,
                    CIP_FILE_ATTR_STATE,
                    CIP_USINT,
                    (void*) &rt_fo->state,
                    CIP_ATTRIB_GETABLE );

            // Instance Name
            rt_fo->instance_name = dir[count].instance_name;
            insertAttribute( file_object_instance,
                    CIP_FILE_ATTR_INSTANCE_NAME,
                    CIP_STRINGI,
                    (void*) rt_fo->instance_name,
                    CIP_ATTRIB_GETABLE );

            // Format Version
            rt_fo->format_version = 0;
            insertAttribute( file_object_instance, CIP_FILE_ATTR_INST_FORMAT_V, CIP_UINT,
                    (void*) &rt_fo->format_version, CIP_ATTRIB_GETABLE );

            // File Name
            rt_fo->file_name = dir[count].file_name;
            insertAttribute( file_object_instance, CIP_FILE_ATTR_FILE_NAME, CIP_STRINGI,
                    (void*) rt_fo->file_name, CIP_ATTRIB_SETABLE );

            // File Revision
            rt_fo->rev.MajorRevision    = 0;
            rt_fo->rev.MinorRevision    = 0;
            insertAttribute( file_object_instance,
                    CIP_FILE_ATTR_FILE_REV,
                    CIP_UINT,
                    (void*) &rt_fo->rev,
                    CIP_ATTRIB_GETABLE );

            // File Size
            rt_fo->file_size = 0;
            insertAttribute( file_object_instance,
                    CIP_FILE_ATTR_FILE_SIZE,
                    CIP_UDINT,
                    (void*) &rt_fo->file_size,
                    CIP_ATTRIB_GETABLE );

            // Checksum
            rt_fo->checksum = 0;
            insertAttribute( file_object_instance, CIP_FILE_ATTR_FILE_CHECKSUM, CIP_INT,
                    (void*) &rt_fo->checksum, CIP_ATTRIB_GETABLE );

            // Invocation method
            rt_fo->invoc_method = 0;
            insertAttribute( file_object_instance, CIP_FILE_ATTR_INVOC_METHOD, CIP_USINT,
                    (void*) &rt_fo->invoc_method, CIP_ATTRIB_GETABLE );

            // File save parameters
            rt_fo->file_save_params = 0;
            insertAttribute( file_object_instance, CIP_FILE_ATTR_FILE_SAVE_PRMS, CIP_BYTE,
                    (void*) &rt_fo->file_save_params, CIP_ATTRIB_GETABLE );

            // File type
            rt_fo->file_type = 0;
            insertAttribute( file_object_instance,
                    CIP_FILE_ATTR_FILE_TYPE,
                    CIP_USINT,
                    (void*) &rt_fo->file_type,
                    CIP_ATTRIB_SETABLE );

            // Services
            insertService( file_object_class,
                    CIP_FILE_SERV_INIT_UPLOAD,
                    &initiate_upload,
                    "Initiate_Upload" );
            insertService( file_object_class,
                    CIP_FILE_SERV_UPLOAD_TRANSFER,
                    &upload_transfer,
                    "Upload_Transfer" );
            insertService( file_object_class,
                    CIP_FILE_SERV_INIT_DOWNLOAD,
                    &initiate_download,
                    "Initiate_Download" );
            insertService( file_object_class,
                    CIP_FILE_SERV_DOWNPLOAD_TRANSFER,
                    &download_transfer,
                    "Download_Transfer" );

            CIPSTER_TRACE_INFO( "An instance #%i of the file object is created", instance_id );

            // Check file
            FILE* f =
                IApp_FileOpen( (char*) dir[count].file_name->strings[0].content->String, "r" );

            if( f == NULL )
            {
                // Create the file if it doesn't exist
                CIPSTER_TRACE_WARN( "The file %s hasn't been found. The file will be created.",
                        (char*) dir[count].file_name->strings[0].content->String );

                f = IApp_FileOpen( (char*) dir[count].file_name->strings[0].content->String, "w" );

                if( f != NULL )
                {
                    IApp_FileClose( f );
                    rt_fo->state = CIP_FILE_STATE_EMPTY;
                }
                else
                    CIPSTER_TRACE_ERR( "The file %s isn't created.",
                            (char*) dir[count].file_name->strings[0].content->String );
            }
            else
            {
                // Figure out the size of the file
                rt_fo->file_size = IApp_FileSize( f );

                if( rt_fo->file_size == 0 )
                    rt_fo->state = CIP_FILE_STATE_EMPTY;
                else
                {
                    // If the file isn't empty, calculate the checksum
                    rt_fo->checksum = 0;
                    int b = 0;

                    while( ( b = IApp_FileGetChar( f ) ) != EOF )
                    {
                        cip_file_object_calc_checksum( &rt_fo->checksum, (EipUint8) b );
                    }

                    rt_fo->state = CIP_FILE_STATE_LOADED;
                }

                IApp_FileClose( f );
            }
        }
        else
            CIPSTER_TRACE_ERR( "The limit %i of number of files is reached.",
                    CIP_FILE_MAX_NUM_INSTANCES );
    }
    else
        CIPSTER_TRACE_ERR( "The File Object class hasn't been found.",
                CIP_FILE_MAX_NUM_INSTANCES );

    return file_object_instance;
}


EipStatus initiate_upload( CipInstance* instance,
        S_CIP_MR_Request* req, S_CIP_MR_Response* resp )
{
    EipUint8 transfer_size;
    EipUint8* ptr_data = resp->Data;
    cip_file_object_struct* rt_fo = (cip_file_object_struct*) instance->rtData;

    decodeData( CIP_USINT, (void*) &transfer_size, &req->Data );

    if( transfer_size > CIP_FILE_MAX_TRANSFER_SIZE )
        transfer_size = CIP_FILE_MAX_TRANSFER_SIZE;

    cip_file_transfer* tr = rt_fo->transfer;

    tr->transfer_size = transfer_size;
    tr->file_name = get_file_name( instance );
    tr->transfer_num = 0;


    resp->GeneralStatus = 0;
    resp->SizeofAdditionalStatus = CIP_ERROR_SUCCESS;
    resp->ReplyService  = (0x80 | req->Service);
    resp->DataLength    = 0;
    resp->DataLength    +=
        encodeData( CIP_UDINT, getAttribute( instance,
                        CIP_FILE_ATTR_FILE_SIZE )->pt2data, &ptr_data );

    resp->DataLength += encodeData( CIP_USINT, (void*) &transfer_size, &ptr_data );

    rt_fo->state = CIP_FILE_STATE_UPLOAD_INIT;

    CIPSTER_TRACE_INFO( "The uploading for object #%i has been initiated.", instance->nInstanceNr );

    return EIP_OK_SEND;
}


EipStatus upload_transfer( CipInstance* instance,
        S_CIP_MR_Request* req, S_CIP_MR_Response* resp )
{
    cip_file_object_struct* rt_fo = (cip_file_object_struct*) instance->rtData;

    // Check the state
    if( rt_fo->state != CIP_FILE_STATE_UPLOAD_INIT
        && rt_fo->state != CIP_FILE_STATE_UPLOAD )
    {
        resp->GeneralStatus = CIP_ERROR_STATE_CONFLICT;
        resp->SizeofAdditionalStatus = 0;
        resp->ReplyService = (0x80 | req->Service);
        resp->DataLength = 0;

        CIPSTER_TRACE_ERR(
                "The file object #%i cannot perform the request service %x in its current state.\n",
                instance->nInstanceNr,
                req->Service );
    }
    else
    {
        EipUint8 transfer_number;

        cip_file_transfer* tr = rt_fo->transfer;

        decodeData( CIP_USINT, (void*) &transfer_number, &req->Data );

        // Check the sequence of transfers
        if( (EipUint8) tr->transfer_num != transfer_number )
        {
            resp->GeneralStatus = CIP_ERROR_INVALID_PARAMETER;
            resp->SizeofAdditionalStatus = 1;
            resp->AdditionalStatus[0] = CIP_FILE_ERR_TRANSFER_OUT_OF_SEQ;
            resp->ReplyService = (0x80 | req->Service);
            resp->DataLength = 0;

            CIPSTER_TRACE_ERR( "The sequence of transfers for file object #%i is wrong.",
                    instance->nInstanceNr );
        }
        else
        {
            EipUint8   transfer_packeg_type;
            EipUint8   buffer[CIP_FILE_MAX_TRANSFER_SIZE];
            S_CIP_Byte_Array file_data;

            // Read file
            file_data.Data = buffer;
            FILE* f = IApp_FileOpen( tr->file_name, "r" );
            IApp_FileSeek( f, tr->transfer_size * tr->transfer_num );
            int count = IApp_FileRead( file_data.Data, sizeof(EipUint8), tr->transfer_size, f );
            IApp_FileClose( f );

            if( count != tr->transfer_size )
            {
                // The end of the file
                file_data.len = count;
                transfer_packeg_type =
                    (rt_fo->state ==
                     CIP_FILE_STATE_UPLOAD_INIT ? CIP_FILE_FL_TRANSFER_PACKET :
                     CIP_FILE_LAST_TRANSFER_PACKET );

                rt_fo->state = CIP_FILE_STATE_LOADED;
            }
            else
            {
                file_data.len = tr->transfer_size;
                transfer_packeg_type =
                    (rt_fo->state ==
                     CIP_FILE_STATE_UPLOAD_INIT ? CIP_FILE_FIRST_TRANSFER_PACKET :
                     CIP_FILE_MIDDLE_TRANSFER_PACKET );

                rt_fo->state = CIP_FILE_STATE_UPLOAD;
            }

            // Build the response
            EipUint8* ptr_data = resp->Data;
            resp->GeneralStatus = CIP_ERROR_SUCCESS;
            resp->SizeofAdditionalStatus = 0;
            resp->ReplyService  = (0x80 | req->Service);
            resp->DataLength    = 0;
            resp->DataLength    += encodeData( CIP_USINT, (void*) &transfer_number, &ptr_data );
            resp->DataLength    +=
                encodeData( CIP_USINT, (void*) &transfer_packeg_type, &ptr_data );
            resp->DataLength += encodeData( CIP_BYTE_ARRAY, (void*) &file_data, &ptr_data );

            if( transfer_packeg_type == CIP_FILE_FL_TRANSFER_PACKET
             || transfer_packeg_type == CIP_FILE_LAST_TRANSFER_PACKET )
            {
                // Add checksum in the end of response
                resp->DataLength += encodeData( CIP_UINT,
                        getAttribute( instance, CIP_FILE_ATTR_FILE_CHECKSUM )->pt2data,
                        &ptr_data );
            }

            tr->transfer_num++;
        }   // else (session->transfer_num  != transfer_number)
    }       // Check the state

    return EIP_OK_SEND;
}


EipStatus initiate_download( CipInstance* instance,
        S_CIP_MR_Request* req, S_CIP_MR_Response* resp )
{
    cip_file_object_struct* rt_fo = (cip_file_object_struct*) instance->rtData;

    EipUint32  file_size;
    EipUint16  format_version;
    S_CIP_Revision file_revision;

    // Decode the request
    decodeData( CIP_UDINT, (void*) &file_size, &req->Data );
    decodeData( CIP_UINT, (void*) &format_version, &req->Data );
    decodeData( CIP_USINT_USINT, (void*) &file_revision, &req->Data );

    S_CIP_String_Int* file_name = cip_string_int_create_from_ary( &req->Data, NULL );

    // Check the file name
    S_CIP_dir_file_struct* files = (S_CIP_dir_file_struct*) getAttribute(
             (CipInstance*) instance->pstClass, CIP_FILE_ATTR_DIRECTORY )->pt2data;

    EipUint16 count = *(EipUint16*) getAttribute( (CipInstance*) instance->pstClass,
            CIP_STD_ATTR_NUMBER_OF_INSTANCE )->pt2data;

    for( int i = 0; i < count; ++i )
    {
        if( files[i].instance_number != instance->nInstanceNr )
        {
            if( cip_string_int_cmp(
                        file_name, files[i].file_name ) == 0 )
            {
                // There is a object with the same file name
                resp->GeneralStatus = CIP_ERROR_INVALID_PARAMETER;

                CIPSTER_TRACE_ERR( "The file with name %s already exists.\n",
                        file_name->strings[0].content->String );

                rt_fo->state = CIP_FILE_STATE_EMPTY;

                break;
            }
        }
    }

    if( resp->GeneralStatus != CIP_ERROR_INVALID_PARAMETER )
    {
        /* Remove old file
         * TODO It should be made more smart to save old version if something wrong happened */
        IApp_FileRemove( get_file_name( instance ) );
        // Register a session
        cip_file_transfer* tr = ( (cip_file_object_struct*) instance->rtData )->transfer;

        tr->checksum = 0;
        tr->transfer_num = 0;
        tr->nbyte = 0;
        tr->file_name = IApp_CipCalloc( file_name->strings[0].content->Length, sizeof(char) );
        strcpy( tr->file_name, (char*) file_name->strings[0].content->String );
        tr->transfer_size   = CIP_FILE_MAX_TRANSFER_SIZE;
        tr->format_version  = format_version;
        tr->file_revision.MajorRevision = file_revision.MajorRevision;
        tr->file_revision.MinorRevision = file_revision.MinorRevision;

        resp->GeneralStatus = CIP_ERROR_SUCCESS;

        // Encode the response data
        EipUint32  inc_burn_num = CIP_FILE_INCREMENTAL_BURN_NUMBER;
        EipUint16  inc_burn_time = CIP_FILE_INCREMENTAL_BURN_TIME;
        EipUint8   tranfer_size = CIP_FILE_MAX_TRANSFER_SIZE;
        EipUint8*  ptr_data = resp->Data;

        resp->DataLength    = 0;
        resp->DataLength    += encodeData( CIP_UDINT, (void*) &inc_burn_num, &ptr_data );
        resp->DataLength    += encodeData( CIP_UINT, (void*) &inc_burn_time, &ptr_data );
        resp->DataLength    += encodeData( CIP_USINT, (void*) &tranfer_size, &ptr_data );


        rt_fo->state = CIP_FILE_STATE_DOWNLOAD_INIT;
    }

    resp->ReplyService = (0x80 | CIP_FILE_SERV_INIT_DOWNLOAD);
    resp->SizeofAdditionalStatus = 0;

    cip_string_int_free( file_name );

    return EIP_OK_SEND;
}


void erase_file( const char* file_name, CipInstance* instance )
{
    FILE* f = IApp_FileOpen( file_name, "w" );

    *(EipUint8*) getAttribute( instance, CIP_FILE_ATTR_STATE )->pt2data = CIP_FILE_STATE_EMPTY;

    IApp_FileClose( f );
}


EipStatus download_transfer( CipInstance* instance,
        S_CIP_MR_Request* req, S_CIP_MR_Response* resp )
{
    cip_file_object_struct* rt_fo = (cip_file_object_struct*) instance->rtData;

    // Check the state
    if( rt_fo->state != CIP_FILE_STATE_DOWNLOAD_INIT
        && rt_fo->state != CIP_FILE_STATE_DOWNLOAD )
    {
        resp->GeneralStatus = CIP_ERROR_STATE_CONFLICT;
        resp->SizeofAdditionalStatus = 0;
        resp->ReplyService = (0x80 | req->Service);
        resp->DataLength = 0;

        CIPSTER_TRACE_ERR(
                "The file object #%i cannot perform the request service %x in its current state.\n",
                instance->nInstanceNr,
                req->Service );
    }
    else
    {
        EipUint8 transfer_number, transfer_packet_type;

        decodeData( CIP_USINT, (void*) &transfer_number, &req->Data );
        decodeData( CIP_USINT, (void*) &transfer_packet_type, &req->Data );

        int array_size = (transfer_packet_type == CIP_FILE_FIRST_TRANSFER_PACKET
                          || transfer_packet_type ==
                          CIP_FILE_MIDDLE_TRANSFER_PACKET ? req->DataLength -
                          2 : req->DataLength - 4);

        EipUint8 i;
        cip_file_transfer* tr = rt_fo->transfer;

        // Check the sequence of the transfers
        if( (EIP_BYTE) tr->transfer_num != transfer_number )
        {
            resp->GeneralStatus = CIP_ERROR_INVALID_PARAMETER;
            resp->SizeofAdditionalStatus = 1;
            resp->AdditionalStatus[0] = CIP_FILE_ERR_TRANSFER_OUT_OF_SEQ;
            resp->ReplyService = (0x80 | req->Service);
            resp->DataLength = 0;

            rt_fo->state = CIP_FILE_STATE_LOADED;

            CIPSTER_TRACE_ERR( "The sequence of transfers for file object #%i is wrong.\n",
                    instance->nInstanceNr );

            erase_file( tr->file_name, instance );
        }
        else
        {
            // Write received data to the file
            FILE* f = IApp_FileOpen( tr->file_name, "a+" );
            IApp_FileWrite( (void*) req->Data, sizeof(EIP_BYTE), array_size, f );
            IApp_FileClose( f );

            for( i = 0; i < array_size; ++i )
            {
                cip_file_object_calc_checksum( &tr->checksum, *req->Data );
                req->Data++;
            }

            tr->nbyte += array_size;

            EipUint8* ptr_data = resp->Data;
            resp->GeneralStatus = CIP_ERROR_SUCCESS;
            resp->SizeofAdditionalStatus = 0;
            resp->ReplyService  = (0x80 | CIP_FILE_SERV_DOWNPLOAD_TRANSFER);
            resp->DataLength    = 0;
            resp->DataLength    += encodeData( CIP_USINT, (void*) &transfer_number, &ptr_data );

            // Check the checksum if it's the last packet
            EipUint16 received_checksum;

            if( transfer_packet_type == CIP_FILE_LAST_TRANSFER_PACKET
                || transfer_packet_type == CIP_FILE_FL_TRANSFER_PACKET )
            {
                decodeData( CIP_UINT, (void*) &received_checksum, &req->Data );

                if( received_checksum != tr->checksum )
                {
                    // Checksum hasn't matched - erase the file and set EMPTY state
                    erase_file( tr->file_name, instance );
                }
                else
                {
                    resp->GeneralStatus = CIP_ERROR_SUCCESS;
                    resp->SizeofAdditionalStatus = 0;

                    // Update the attributes
                    *(EipUint32*) getAttribute( instance,
                            CIP_FILE_ATTR_FILE_SIZE )->pt2data = tr->nbyte;
                    *(EipUint16*) getAttribute( instance,
                            CIP_FILE_ATTR_FILE_CHECKSUM )->pt2data   = tr->checksum;
                    *(EipUint16*) getAttribute( instance,
                            CIP_FILE_ATTR_INST_FORMAT_V )->pt2data   = tr->format_version;

                    S_CIP_Revision* rev = (S_CIP_Revision*) getAttribute( instance,
                            CIP_FILE_ATTR_FILE_REV )->pt2data;
                    rev->MajorRevision  = tr->file_revision.MajorRevision;
                    rev->MinorRevision  = tr->file_revision.MinorRevision;

                    rt_fo->state = CIP_FILE_STATE_LOADED;
                }
            }
            else
            {
                // Continue the transferring
                resp->GeneralStatus = CIP_ERROR_SUCCESS;
                resp->SizeofAdditionalStatus = 0;

                rt_fo->state = CIP_FILE_STATE_DOWNLOAD;

                tr->transfer_num++;
            }
        }   // Check the sequence of the transfers
    }       // Check the state

    return EIP_OK_SEND;
}


char* get_file_name( CipInstance* instance )
{
    S_CIP_String_Int* stri = getAttribute( instance, CIP_FILE_ATTR_FILE_NAME )->pt2data;

    return (char*) stri->strings[0].content->String;
}


void cip_file_object_calc_checksum( EipUint16* sum, EipUint8 byte )
{
    EipUint32 sum_ = *sum + byte;

    if( sum_ >= 0x10000 )
    {
        sum_    &= 0x0000ffff;
        sum_    = 0x10000 - sum_;
    }

    *sum = (EipUint16) sum_;
}


EipStatus getAttributeSingleFileObject( CipInstance* instance,
        S_CIP_MR_Request* req, S_CIP_MR_Response* resp )
{
    if( req->RequestPath.AttributNr != CIP_FILE_ATTR_DIRECTORY )
        return getAttributeSingle( instance, req, resp );
    else
    {
        // Encode the content of the directory
        S_CIP_attribute_struct* dir = getAttribute( instance, CIP_FILE_ATTR_DIRECTORY );
        EipUint16 num =
            *(EipUint16*) getAttribute( instance, CIP_STD_ATTR_NUMBER_OF_INSTANCE )->pt2data;
        S_CIP_dir_file_struct* files = (S_CIP_dir_file_struct*) dir->pt2data;

        EipUint16  i;
        EipUint8*  ptr_data = resp->Data;
        resp->DataLength = 0;
        resp->ReplyService  = (0x80 | req->Service);
        resp->GeneralStatus = CIP_ERROR_SUCCESS;
        resp->SizeofAdditionalStatus = 0;

        for( i = 0; i < num; ++i )
        {
            resp->DataLength    += encodeData( CIP_UINT,
                    (void*) &files[i].instance_number,
                    &ptr_data );
            resp->DataLength    += encodeData( CIP_STRINGI,
                    (void*) files[i].instance_name,
                    &ptr_data );
            resp->DataLength    += encodeData( CIP_STRINGI, (void*) files[i].file_name, &ptr_data );
        }
    }

    return EIP_OK_SEND;
}
