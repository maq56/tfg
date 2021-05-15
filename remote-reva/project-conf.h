#ifndef PROJECT_CONF_H_
#define PROJECT_CONF_H_

/*----------------------------------------------------------------------------*/
/* GENERAL CONFIG                                                             */
/*----------------------------------------------------------------------------*/
#undef LPM_CONF_ENABLE
#define LPM_CONF_ENABLE 0

/*----------------------------------------------------------------------------*/
/* RPL CONFIG                                                                 */
/*----------------------------------------------------------------------------*/
#undef NBR_TABLE_CONF_MAX_NEIGHBORS
#undef UIP_CONF_MAX_ROUTES

#define NBR_TABLE_CONF_MAX_NEIGHBORS                10
#define UIP_CONF_MAX_ROUTES                         10

#undef NETSTACK_CONF_RDC
#define NETSTACK_CONF_RDC                           nullrdc_driver
#undef NULLRDC_CONF_802154_AUTOACK
#define NULLRDC_CONF_802154_AUTOACK                 1

#undef  NETSTACK_CONF_RADIO
#define NETSTACK_CONF_RADIO                         cc2538_rf_driver

#define ANTENNA_SW_SELECT_DEF_CONF                  ANTENNA_SW_SELECT_2_4GHZ

#define RESOLV_CONF_SUPPORTS_MDNS                   0

/*----------------------------------------------------------------------------*/
/* APP CONFIG FOR REMOTE TASKS                                                */
/*----------------------------------------------------------------------------*/
// no defines required here for app.

#endif
