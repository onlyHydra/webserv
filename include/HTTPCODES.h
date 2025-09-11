/* --- HttpCodes.h --- */

/* ------------------------------------------
Author: Lilly81324
Date: 11/08/2025
------------------------------------------ */

#ifndef HTTPCODES_H
# define HTTPCODES_H

// Informational responses------------------

// Request succeeded
# define HTTP_OK 200

// File was created
# define HTTP_FILE_CREATED 201

// Server cannot or will not process request
# define HTTP_BAD_REQUEST 400

// Client is unauthenticated
# define HTTP_UNATH 401

// Client does not have access to requested content
# define HTTP_FORBIDDEN 403

// Request target cannot be found
# define HTTP_NOT_FOUND 404

// Request has forbidden method
# define HTTP_METHOD_FORBIDDEN 405

// Request conflicts with current state of server
# define HTTP_CONFLICT 409

// Requests ETag does not match the current ETag (file out of date)
# define HTTP_PRECON_FAIL 412

// Unsupported Media Type
# define HTTP_INV_MEDIA 415

// Header fiels are too big
# define HTTP_HEADER_TOO_BIG 431

// Server encountered unexpected condition stopping it from fulfilling request
# define HTTP_INV_SERVER_ERR 500

// When server overloads, network fails or other server-side error
# define HTTP_SERVER_ERROR 599

#endif // HTTPCODES_H