/*
 * FreeRTOS+TCP Labs Build 141019 (C) 2014 Real Time Engineers ltd.
 * Authors include Hein Tibosch and Richard Barry
 *
 *******************************************************************************
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 ***                                                                         ***
 ***                                                                         ***
 ***   FREERTOS+TCP IS STILL IN THE LAB:                                     ***
 ***                                                                         ***
 ***   This product is functional and is already being used in commercial    ***
 ***   products.  Be aware however that we are still refining its design,    ***
 ***   the source code does not yet fully conform to the strict coding and   ***
 ***   style standards mandated by Real Time Engineers ltd., and the         ***
 ***   documentation and testing is not necessarily complete.                ***
 ***                                                                         ***
 ***   PLEASE REPORT EXPERIENCES USING THE SUPPORT RESOURCES FOUND ON THE    ***
 ***   URL: http://www.FreeRTOS.org/contact  Active early adopters may, at   ***
 ***   the sole discretion of Real Time Engineers Ltd., be offered versions  ***
 ***   under a license other than that described below.                      ***
 ***                                                                         ***
 ***                                                                         ***
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 *******************************************************************************
 *
 * - Open source licensing -
 * While FreeRTOS+TCP is in the lab it is provided only under version two of the
 * GNU General Public License (GPL) (which is different to the standard FreeRTOS
 * license).  FreeRTOS+TCP is free to download, use and distribute under the
 * terms of that license provided the copyright notice and this text are not
 * altered or removed from the source files.  The GPL V2 text is available on
 * the gnu.org web site, and on the following
 * URL: http://www.FreeRTOS.org/gpl-2.0.txt.  Active early adopters may, and
 * solely at the discretion of Real Time Engineers Ltd., be offered versions
 * under a license other then the GPL.
 *
 * FreeRTOS+TCP is distributed in the hope that it will be useful.  You cannot
 * use FreeRTOS+TCP unless you agree that you use the software 'as is'.
 * FreeRTOS+TCP is provided WITHOUT ANY WARRANTY; without even the implied
 * warranties of NON-INFRINGEMENT, MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. Real Time Engineers Ltd. disclaims all conditions and terms, be they
 * implied, expressed, or statutory.
 *
 * 1 tab == 4 spaces!
 *
 * http://www.FreeRTOS.org
 * http://www.FreeRTOS.org/udp
 *
 */


/******************************************************************************
 *
 * See the following web page for essential buffer allocation scheme usage and
 * configuration details:
 * http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/Embedded_Ethernet_Buffer_Management.html
 *
 ******************************************************************************/


/* Standard includes. */
#include <stdint.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS-Plus-TCP/FreeRTOS_IP.h"
#include "FreeRTOS-Plus-TCP/FreeRTOS_Sockets.h"
#include "FreeRTOS-Plus-TCP/FreeRTOS_IP_Private.h"
#include "FreeRTOS-Plus-TCP/FreeRTOS_UDP_IP.h"
#include "FreeRTOS-Plus-TCP/NetworkInterface.h"
#include "FreeRTOS-Plus-TCP/NetworkBufferManagement.h"

/* For an Ethernet interrupt to be able to obtain a network buffer there must
be at least this number of buffers available. */
#define ipINTERRUPT_BUFFER_GET_THRESHOLD	( 3 )

/* A list of free (available) xNetworkBufferDescriptor_t structures. */
static List_t xFreeBuffersList;

/* Declares the pool of xNetworkBufferDescriptor_t structures that are available to the
system.  All the network buffers referenced from xFreeBuffersList exist in this
array.  The array is not accessed directly except during initialisation, when
the xFreeBuffersList is filled (as all the buffers are free when the system is
booted). */
static xNetworkBufferDescriptor_t xNetworkBuffers[ ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS ];

/* This constant is defined as true to let FreeRTOS_TCP_IP.c know that the network buffers
have constant size, large enough to hold the biggest ethernet packet. No resizing will
be done. */
const BaseType_t xBufferAllocFixedSize = pdTRUE;

/* The semaphore used to obtain network buffers. */
static SemaphoreHandle_t xNetworkBufferSemaphore = NULL;

#if !defined( ipconfigBUFFER_ALLOC_LOCK )
	#define ipconfigBUFFER_ALLOC_INIT( ) do {} while (0)
	#define ipconfigBUFFER_ALLOC_LOCK_FROM_ISR()		\
		UBaseType_t uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR(); \
		{

	#define ipconfigBUFFER_ALLOC_UNLOCK_FROM_ISR()		\
			portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus ); \
		}

	#define ipconfigBUFFER_ALLOC_LOCK()					taskENTER_CRITICAL()
	#define ipconfigBUFFER_ALLOC_UNLOCK()				taskEXIT_CRITICAL()
#else
	/* user can define hist own lock/unlock routines */
#endif /* ipconfigBUFFER_ALLOC_LOCK */

/*-----------------------------------------------------------*/

#if( ipconfigIP_TASK_KEEPS_MESSAGE_BUFFER != 0 )

	/* when this options is used, the IP-task will keep 1 message buffer permanently
	for its own use. */
	extern xNetworkBufferDescriptor_t *pxIpTaskMessageBuffer;

#endif

#if ipconfigTCP_IP_SANITY

/* HT: SANITY code will be removed as soon as the library is stable
 * and and ready to become public
 * Function below gives information about the use of buffers */
#define WARN_LOW		( 2 )
#define WARN_HIGH		( ( 5 * ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS ) / 10 )

static char cIsLow = pdFALSE;

/*-----------------------------------------------------------*/

BaseType_t prvIsFreeBuffer( const xNetworkBufferDescriptor_t *pxDescr )
{
	return ( bIsValidNetworkDescriptor( pxDescr ) != 0 ) &&
		( listIS_CONTAINED_WITHIN( &xFreeBuffersList, &( pxDescr->xBufferListItem ) ) != 0 );
}
/*-----------------------------------------------------------*/

static void showWarnings( void )
{
	UBaseType_t uxCount = uxGetNumberOfFreeNetworkBuffers( );
	if( ( ( cIsLow == 0 ) && ( uxCount <= WARN_LOW ) ) || ( ( cIsLow != 0 ) && ( uxCount >= WARN_HIGH ) ) )
	{
		cIsLow = !cIsLow;
		FreeRTOS_debug_printf( ( "*** Warning *** %s %lu buffers left\n", cIsLow ? "only" : "now", uxCount ) );
	}
	else
	{
		return;
	}
}
/*-----------------------------------------------------------*/

UBaseType_t bIsValidNetworkDescriptor( const xNetworkBufferDescriptor_t * pxDesc )
{
	uint32_t offset = ( uint32_t ) ( ((const char *)pxDesc) - ((const char *)xNetworkBuffers) );
	if( ( offset >= sizeof( xNetworkBuffers ) ) ||
		( ( offset % sizeof( xNetworkBuffers[0] ) ) != 0 ) )
		return pdFALSE;
	return (UBaseType_t) (pxDesc - xNetworkBuffers) + 1;
}

#else

UBaseType_t bIsValidNetworkDescriptor (const xNetworkBufferDescriptor_t * pxDesc);

UBaseType_t bIsValidNetworkDescriptor (const xNetworkBufferDescriptor_t * pxDesc)
{
	(void)pxDesc;
	return pdTRUE;
}
/*-----------------------------------------------------------*/

static void showWarnings( void )
{
}

#endif /* ipconfigTCP_IP_SANITY */

BaseType_t xNetworkBuffersInitialise( void )
{
BaseType_t xReturn, x;

	/* Only initialise the buffers and their associated kernel objects if they
	have not been initialised before. */
	if( xNetworkBufferSemaphore == NULL )
	{
		/* In case alternative locking is used, the mutexes can be initialised here */
		ipconfigBUFFER_ALLOC_INIT( );
/*
 * HT: The use of counting semaphores is perfect, good invention
 * It is just that I didn't find the time yet to adopt the newer FreeRTOS kernel
 * which supports it. I tried it and it crashed. Must dive into it some day
 */
		xNetworkBufferSemaphore = xSemaphoreCreateCounting( ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS, ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS );
		configASSERT( xNetworkBufferSemaphore );

		if( xNetworkBufferSemaphore != NULL )
		{
			vListInitialise( &xFreeBuffersList );

			/* Initialise all the network buffers.  The buffer storage comes
			from the network interface, and different hardware has different
			requirements. */
			vNetworkInterfaceAllocateRAMToBuffers( xNetworkBuffers );
			for( x = 0; x < ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS; x++ )
			{
				/* Initialise and set the owner of the buffer list items. */
				vListInitialiseItem( &( xNetworkBuffers[ x ].xBufferListItem ) );
				listSET_LIST_ITEM_OWNER( &( xNetworkBuffers[ x ].xBufferListItem ), &xNetworkBuffers[ x ] );

				/* Currently, all buffers are available for use. */
				vListInsert( &xFreeBuffersList, &( xNetworkBuffers[ x ].xBufferListItem ) );
			}
		}
	}

	if( xNetworkBufferSemaphore == NULL )
	{
		xReturn = pdFAIL;
	}
	else
	{
		xReturn = pdPASS;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

xNetworkBufferDescriptor_t *pxGetNetworkBufferWithDescriptor( size_t xRequestedSizeBytes, TickType_t xBlockTimeTicks )
{
xNetworkBufferDescriptor_t *pxReturn = NULL;
BaseType_t xInvalid = pdFALSE;

	/* The current implementation only has a single size memory block, so
	the requested size parameter is not used (yet). */
	( void ) xRequestedSizeBytes;
	if( xNetworkBufferSemaphore != NULL )
	{
		/* If there is a semaphore available, there is a network buffer available. */
		if( xSemaphoreTake( xNetworkBufferSemaphore, xBlockTimeTicks ) == pdPASS )
		{
			/* Protect the structure as it is accessed from tasks and interrupts. */
			ipconfigBUFFER_ALLOC_LOCK();
			{
				pxReturn = ( xNetworkBufferDescriptor_t * ) listGET_OWNER_OF_HEAD_ENTRY( &xFreeBuffersList );
				if( bIsValidNetworkDescriptor(pxReturn) &&
					listIS_CONTAINED_WITHIN( &xFreeBuffersList, &( pxReturn->xBufferListItem ) ) )
				{
					uxListRemove( &( pxReturn->xBufferListItem ) );
				}
				else
				{
					xInvalid = pdTRUE;
				}
			}
			ipconfigBUFFER_ALLOC_UNLOCK();

			if( xInvalid )
			{
				FreeRTOS_debug_printf( ( "pxGetNetworkBufferWithDescriptor: INVALID BUFFER: %p (valid %lu)\n",
					pxReturn, bIsValidNetworkDescriptor( pxReturn ) ) );
				pxReturn = NULL;
			}
			else
			{
				pxReturn->xDataLength = xRequestedSizeBytes;

				#if( ipconfigTCP_IP_SANITY != 0 )
				{
					showWarnings();
				}
				#endif /* ipconfigTCP_IP_SANITY */

				#if( ipconfigUSE_LINKED_RX_MESSAGES != 0 )
				{
					/* make sure the buffer is not linked */
					pxReturn->pxNextBuffer = NULL;
				}
				#endif /* ipconfigUSE_LINKED_RX_MESSAGES */

				if( xTCPWindowLoggingLevel > 3 )
				{
					FreeRTOS_debug_printf( ( "BUF_GET[%ld]: %p (%p)\n",
						bIsValidNetworkDescriptor( pxReturn ),
						pxReturn, pxReturn->pucEthernetBuffer ) );
				}
			}
			iptraceNETWORK_BUFFER_OBTAINED( pxReturn );
		}
		else
		{
			iptraceFAILED_TO_OBTAIN_NETWORK_BUFFER();
		}
	}

	return pxReturn;
}
/*-----------------------------------------------------------*/

xNetworkBufferDescriptor_t *pxNetworkBufferGetFromISR( size_t xRequestedSizeBytes )
{
xNetworkBufferDescriptor_t *pxReturn = NULL;

	/* The current implementation only has a single size memory block, so
	the requested size parameter is not used (yet). */
	( void ) xRequestedSizeBytes;

	/* If there is a semaphore available then there is a buffer available, but,
	as this is called from an interrupt, only take a buffer if there are at
	least ipINTERRUPT_BUFFER_GET_THRESHOLD buffers remaining.  This prevents,
	to a certain degree at least, a rapidly executing interrupt exhausting
	buffer and in so doing preventing tasks from continuing. */
	if( uxQueueMessagesWaitingFromISR( ( QueueHandle_t ) xNetworkBufferSemaphore ) > ipINTERRUPT_BUFFER_GET_THRESHOLD )
	{
		if( xSemaphoreTakeFromISR( xNetworkBufferSemaphore, NULL ) == pdPASS )
		{
			/* Protect the structure as it is accessed from tasks and interrupts. */
			ipconfigBUFFER_ALLOC_LOCK_FROM_ISR();
			{
				pxReturn = ( xNetworkBufferDescriptor_t * ) listGET_OWNER_OF_HEAD_ENTRY( &xFreeBuffersList );
				uxListRemove( &( pxReturn->xBufferListItem ) );
			}
			ipconfigBUFFER_ALLOC_UNLOCK_FROM_ISR();

			iptraceNETWORK_BUFFER_OBTAINED_FROM_ISR( pxReturn );
		}
	}

	if( pxReturn == NULL )
	{
		iptraceFAILED_TO_OBTAIN_NETWORK_BUFFER_FROM_ISR();
	}

	return pxReturn;
}
/*-----------------------------------------------------------*/

BaseType_t vNetworkBufferReleaseFromISR( xNetworkBufferDescriptor_t * const pxNetworkBuffer )
{
BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	/* Ensure the buffer is returned to the list of free buffers before the
	counting semaphore is 'given' to say a buffer is available. */
	ipconfigBUFFER_ALLOC_LOCK_FROM_ISR();
	{
		vListInsertEnd( &xFreeBuffersList, &( pxNetworkBuffer->xBufferListItem ) );
	}
	ipconfigBUFFER_ALLOC_UNLOCK_FROM_ISR();

	xSemaphoreGiveFromISR( xNetworkBufferSemaphore, &xHigherPriorityTaskWoken );
	iptraceNETWORK_BUFFER_RELEASED( pxNetworkBuffer );

	return xHigherPriorityTaskWoken;
}
/*-----------------------------------------------------------*/

void vReleaseNetworkBufferAndDescriptor( xNetworkBufferDescriptor_t * const pxNetworkBuffer )
{
BaseType_t xListItemAlreadyInFreeList;

#if( ipconfigIP_TASK_KEEPS_MESSAGE_BUFFER != 0 )
	if( pxNetworkBuffer == pxIpTaskMessageBuffer )
		return;
#endif

	if( !bIsValidNetworkDescriptor ( pxNetworkBuffer ) )
	{
		FreeRTOS_debug_printf( ( "vReleaseNetworkBufferAndDescriptor: Invalid buffer %p\n", pxNetworkBuffer ) );
		return ;
	}
	/* Ensure the buffer is returned to the list of free buffers before the
	counting semaphore is 'given' to say a buffer is available. */
/*	taskENTER_CRITICAL(); */
	ipconfigBUFFER_ALLOC_LOCK();
	{
		{
			xListItemAlreadyInFreeList = listIS_CONTAINED_WITHIN( &xFreeBuffersList, &( pxNetworkBuffer->xBufferListItem ) );

			if( xListItemAlreadyInFreeList == pdFALSE )
			{
				vListInsertEnd( &xFreeBuffersList, &( pxNetworkBuffer->xBufferListItem ) );
			}
		}

/*		configASSERT( xListItemAlreadyInFreeList == pdFALSE ); */
	}
	ipconfigBUFFER_ALLOC_UNLOCK();
/*	taskEXIT_CRITICAL(); */
	if( xListItemAlreadyInFreeList )
	{
		FreeRTOS_debug_printf( ( "vReleaseNetworkBufferAndDescriptor: %p ALREADY RELEASED (now %lu)\n",
			pxNetworkBuffer, uxGetNumberOfFreeNetworkBuffers( ) ) );
	}
	if( !xListItemAlreadyInFreeList )
	{
		xSemaphoreGive( xNetworkBufferSemaphore );
		showWarnings();
		if( xTCPWindowLoggingLevel > 3 )
			FreeRTOS_debug_printf( ( "BUF_PUT[%ld]: %p (%p) (now %lu)\n",
				bIsValidNetworkDescriptor( pxNetworkBuffer ),
				pxNetworkBuffer, pxNetworkBuffer->pucEthernetBuffer,
				uxGetNumberOfFreeNetworkBuffers( ) ) );
	}
	iptraceNETWORK_BUFFER_RELEASED( pxNetworkBuffer );
}
/*-----------------------------------------------------------*/

/*#if( ipconfigINCLUDE_TEST_CODE == 1 ) */

UBaseType_t uxGetNumberOfFreeNetworkBuffers( void )
{
	return listCURRENT_LIST_LENGTH( &xFreeBuffersList );
}

/*#endif */ /* ipconfigINCLUDE_TEST_CODE */