#include <windows.h>
#include <dsgetdc.h>
#include <winldap.h>
#include <winber.h>
#include <rpc.h>
#include <rpcdce.h>
#include <stdint.h>
#include "bofdefs.h"
#include "base.c"
#define SECURITY_WIN32
#include <secext.h> 

#define MAX_ATTRIBUTES 100

LDAP* InitialiseLDAPConnection(PCHAR hostName, PCHAR distinguishedName){
	LDAP* pLdapConnection = NULL;

    pLdapConnection = WLDAP32$ldap_init(hostName, 389);
    
    if (pLdapConnection == NULL)
    {
      	BeaconPrintf(CALLBACK_ERROR, "Failed to establish LDAP connection on 389.");
        return NULL;
    }

	//////////////////////////////
	// Bind to DC
	//////////////////////////////
    ULONG lRtn = 0;

    lRtn = WLDAP32$ldap_bind_s(
                pLdapConnection,      // Session Handle
                distinguishedName,    // Domain DN
                NULL,                 // Credential structure
                LDAP_AUTH_NEGOTIATE); // Auth mode

    if(lRtn != LDAP_SUCCESS)
    {
    	BeaconPrintf(CALLBACK_ERROR, "Bind Failed: %u", lRtn);
        WLDAP32$ldap_unbind(pLdapConnection);
        pLdapConnection = NULL; 
    }
    return pLdapConnection;
}

LDAPMessage* ExecuteLDAPQuery(LDAP* pLdapConnection, PCHAR distinguishedName, char * ldap_filter, char * ldap_attributes){
	
    internal_printf("[*] Filter: %s\n",ldap_filter);

    ULONG errorCode = LDAP_SUCCESS;
    LDAPMessage* pSearchResult = NULL;
    
	if(ldap_attributes){
        internal_printf("[*] Returning specific attribute(s): %s\n",ldap_attributes);

        PCHAR attr[MAX_ATTRIBUTES] = {0};
        
        int attribute_count = 0;
        char *token = NULL;
        const char s[2] = ","; //delimiter

        token = MSVCRT$strtok(ldap_attributes, s);

        while( token != NULL ) {
            if(attribute_count < (MAX_ATTRIBUTES - 1)){
                attr[attribute_count] = token;
                attribute_count++;
                token = MSVCRT$strtok(NULL, s);
            } else{
                internal_printf("[!] Cannot return more than %i attributes, will omit additional attributes.\n", MAX_ATTRIBUTES);
                break;
            }
        }

        attr[attribute_count] = NULL;
        
		errorCode = WLDAP32$ldap_search_s(
        pLdapConnection,    // Session handle
        distinguishedName,  // DN to start search
        LDAP_SCOPE_SUBTREE, // Scope
        ldap_filter,        // Filter
        attr,               // Retrieve list of attributes
        0,                  // Get both attributes and values
        &pSearchResult);    // [out] Search results
	} else {
        internal_printf("[*] Returning all attributes.\n");

		errorCode = WLDAP32$ldap_search_s(
        pLdapConnection,    // Session handle
        distinguishedName,  // DN to start search
        LDAP_SCOPE_SUBTREE, // Scope
        ldap_filter,        // Filter
        NULL,               // Retrieve all attributes
        0,                  // Get both attributes and values
        &pSearchResult);    // [out] Search results
	}   
    
    if (errorCode == LDAP_SIZELIMIT_EXCEEDED) 
    {
        BeaconPrintf(CALLBACK_ERROR, "Search returned more results then server would allow, Results will be incomplete");
    }
    else if (errorCode != LDAP_SUCCESS)
    {
        BeaconPrintf(CALLBACK_ERROR, "LDAP Search Failed: %u", errorCode);

        if(pSearchResult != NULL)
        {
            WLDAP32$ldap_msgfree(pSearchResult);
            pSearchResult = NULL;
        }
    }
    return pSearchResult;

}

void customAttributes(PCHAR pAttribute, PCHAR pValue)
{
    if(MSVCRT$strcmp(pAttribute, "objectGUID") == 0) 
    {
        RPC_CSTR G = NULL;
        RPCRT4$UuidToStringA((UUID *) pValue, &G);
        internal_printf("%s", G);
        RPCRT4$RpcStringFreeA(&G);       
    }
    else if(MSVCRT$strcmp(pAttribute, "objectSid") == 0)
    {
        LPSTR sid = NULL;
        ADVAPI32$ConvertSidToStringSidA((PSID)pValue, &sid);
        internal_printf("%s", sid);
        KERNEL32$LocalFree(sid);
    }
    else
    {
        internal_printf("%s", pValue);
    }
    
}

void printAttribute(PCHAR pAttribute, PCHAR* ppValue){
    internal_printf("\n%s: ", pAttribute);
    customAttributes(pAttribute, *ppValue);
    ppValue++;
    while(*ppValue != NULL)
    {
        internal_printf(", ");
        customAttributes(pAttribute, *ppValue);
        ppValue++;
    }
}

void ldapSearch(char * ldap_filter, char * ldap_attributes,	ULONG results_count){
    char szDN[1024] = {0};
	ULONG ulSize = sizeof(szDN)/sizeof(szDN[0]);
	
    BOOL res = SECUR32$GetUserNameExA(NameFullyQualifiedDN, szDN, &ulSize);
    DWORD dwRet = 0;
    PDOMAIN_CONTROLLER_INFO pdcInfo = NULL;
    LDAP* pLdapConnection = NULL; 
    LDAPMessage* pSearchResult = NULL;
    char* distinguishedName = NULL;
    BerElement* pBer = NULL;

	distinguishedName = MSVCRT$strstr(szDN, "DC");
	if(distinguishedName != NULL) {
    	internal_printf("[*] Distinguished name: %s\n", distinguishedName);	
	}
	else{
		BeaconPrintf(CALLBACK_ERROR, "Failed to retrieve distinguished name.");
        return;

	}

	////////////////////////////
	// Retrieve PDC
	////////////////////////////

    dwRet = NETAPI32$DsGetDcNameA(NULL, NULL, NULL, NULL, 0, &pdcInfo);
    if (ERROR_SUCCESS == dwRet) {
        internal_printf("[*] DC: %s\n", pdcInfo->DomainControllerName);       
    } else {
        BeaconPrintf(CALLBACK_ERROR, "Failed to identify PDC, are we domain joined?");
        goto end;
    }


	//////////////////////////////
	// Initialise LDAP Session
    // Taken from https://docs.microsoft.com/en-us/previous-versions/windows/desktop/ldap/searching-a-directory
	//////////////////////////////
    pLdapConnection = InitialiseLDAPConnection(pdcInfo->DomainControllerName + 2, distinguishedName);

    if(!pLdapConnection)
        {goto end;}

	//////////////////////////////
	// Perform LDAP Search
	//////////////////////////////
	pSearchResult = ExecuteLDAPQuery(pLdapConnection, distinguishedName, ldap_filter, ldap_attributes);    

    if(!pSearchResult)
        {goto end;}

    //////////////////////////////
	// Get Search Result Count
	//////////////////////////////
    DWORD numberOfEntries = WLDAP32$ldap_count_entries(
                        pLdapConnection,    // Session handle
                        pSearchResult);     // Search result
    
    if(numberOfEntries == -1) // -1 is functions return value when it failed
    {
        BeaconPrintf(CALLBACK_ERROR, "Failed to count search results.");
        goto end;
    }
    else if(!numberOfEntries)
    {
        BeaconPrintf(CALLBACK_ERROR, "Search returned zero results");
        goto end;
    }    
    
    //////////////////////////////
	// Get Search Result Count
	//////////////////////////////
    LDAPMessage* pEntry = NULL;
    PCHAR pEntryDN = NULL;
    ULONG iCnt = 0;
    PCHAR pAttribute = NULL;
    PCHAR* ppValue = NULL;

    ULONG results_limit = 0;

    if ((results_count == 0) || (results_count > numberOfEntries)){
		results_limit = numberOfEntries;
  	    internal_printf("\n[*] Result count: %d\n", numberOfEntries);
    }
    else { 
    	results_limit = results_count;
    	internal_printf("\n[*] Result count: %d (showing max. %d)\n", numberOfEntries, results_count);
    }


    for( iCnt=0; iCnt < results_limit; iCnt++ )
    {
       	internal_printf("\n--------------------");

        // Get the first/next entry.
        if( !iCnt )
            {pEntry = WLDAP32$ldap_first_entry(pLdapConnection, pSearchResult);}
        else
            {pEntry = WLDAP32$ldap_next_entry(pLdapConnection, pEntry);}
        
        if( pEntry == NULL )
        {
            break;
        }
                
        // Get the first attribute name.
        pAttribute = WLDAP32$ldap_first_attribute(
                      pLdapConnection,   // Session handle
                      pEntry,            // Current entry
                      &pBer);            // [out] Current BerElement
        
        // Output the attribute names for the current object
        // and output values.
        while(pAttribute != NULL)
        {
            // Get the string values.
            ppValue = WLDAP32$ldap_get_values(
                          pLdapConnection,  // Session Handle
                          pEntry,           // Current entry
                          pAttribute);      // Current attribute

            // Use and Free memory.
            if(ppValue != NULL)  
            {
                printAttribute(pAttribute, ppValue);
                WLDAP32$ldap_value_free(ppValue);
                ppValue = NULL;
            }
            WLDAP32$ldap_memfree(pAttribute);
            
            // Get next attribute name.
            pAttribute = WLDAP32$ldap_next_attribute(
                pLdapConnection,   // Session Handle
                pEntry,            // Current entry
                pBer);             // Current BerElement
        }
        
        if( pBer != NULL )
        {
            WLDAP32$ber_free(pBer,0);
            pBer = NULL;
        }
    }

    end: 
    if( pBer != NULL )
    {
        WLDAP32$ber_free(pBer,0);
        pBer = NULL;
    }
    if(pdcInfo)
    {
        NETAPI32$NetApiBufferFree(pdcInfo);
        pdcInfo = NULL;
    }
    if(pLdapConnection)
    {
        WLDAP32$ldap_unbind(pLdapConnection);
        pLdapConnection = NULL;
    }
    if(pSearchResult)
    {
        WLDAP32$ldap_msgfree(pSearchResult);
        pSearchResult = NULL;
    }
    if (ppValue)
    {
        WLDAP32$ldap_value_free(ppValue);
        ppValue = NULL;
    }    

}


VOID go( 
	IN PCHAR Buffer, 
	IN ULONG Length 
) 
{
	datap  parser;
	char * ldap_filter;
	char * ldap_attributes;
	ULONG results_count;

	BeaconDataParse(&parser, Buffer, Length);
	ldap_filter = BeaconDataExtract(&parser, NULL);
	ldap_attributes = BeaconDataExtract(&parser, NULL);
	results_count = BeaconDataInt(&parser);

    ldap_attributes = *ldap_attributes == 0 ? NULL : ldap_attributes;


	if(!bofstart())
	{
		return;
	}

	ldapSearch(ldap_filter, ldap_attributes, results_count);

	printoutput(TRUE);
	bofstop();
};
