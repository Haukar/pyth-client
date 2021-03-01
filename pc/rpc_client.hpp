#pragma once

#include <pc/net_socket.hpp>
#include <pc/jtree.hpp>
#include <pc/key_pair.hpp>
#include <unordered_map>

#define PC_RPC_ERROR_BLOCK_CLEANED_UP          -32001
#define PC_RPC_ERROR_SEND_TX_PREFLIGHT_FAIL    -32002
#define PC_RPC_ERROR_TX_SIG_VERIFY_FAILURE     -32003
#define PC_RPC_ERROR_BLOCK_NOT_AVAILABLE       -32004
#define PC_RPC_ERROR_NODE_UNHEALTHY            -32005
#define PC_RPC_ERROR_TX_PRECOMPILE_VERIFY_FAIL -32006
#define PC_RPC_ERROR_SLOT_SKIPPED              -32007
#define PC_RPC_ERROR_NO_SNAPSHOT               -32008
#define PC_RPC_ERROR_LONG_TERM_SLOT_SKIPPED    -32009

namespace pc
{

  class rpc_request;

  // solana rpc REST API client
  class rpc_client : public error
  {
  public:

    rpc_client();

    // rpc http connection
    void set_http_conn( net_connect * );
    net_connect *get_http_conn() const;

    // rpc web socket connection
    void set_ws_conn( net_connect * );
    net_connect *get_ws_conn() const;

    // submit rpc request (and bundled callback)
    void send( rpc_request * );

  public:

    // parse json payload and invoke callback
    void parse_response( const char *msg, size_t msg_len );

    // add/remove request from notification map
    void add_notify( rpc_request * );
    void remove_notify( rpc_request * );

  private:

    struct rpc_http : public http_client {
      void parse_content( const char *, size_t ) override;
      rpc_client *cp_;
    };

    struct rpc_ws : public ws_parser {
      void parse_msg( const char *msg, size_t msg_len ) override;
      rpc_client *cp_;
    };

    typedef std::vector<rpc_request*>    request_t;
    typedef std::vector<uint64_t>        id_vec_t;
    typedef std::unordered_map<uint64_t,rpc_request*> sub_map_t;

    net_connect *hptr_;
    net_connect *wptr_;
    rpc_http     hp_;    // http parser wrapper
    rpc_ws       wp_;    // websocket parser wrapper
    jtree        jp_;    // json parser
    request_t    rv_;    // waiting requests by id
    id_vec_t     reuse_; // reuse id list
    sub_map_t    smap_;  // subscription map
    uint64_t     id_;    // next request id
  };

  // rpc response or subscrption callback
  class rpc_sub
  {
  public:
    virtual ~rpc_sub();
  };

  // rpc subscription callback for request type T
  template<class T>
  class rpc_sub_i
  {
  public:
    virtual ~rpc_sub_i() {}
    virtual void on_response( T * ) = 0;
  };

  // base-class rpc request message
  class rpc_request : public error
  {
  public:
    rpc_request();
    virtual ~rpc_request();

    // corresponding rpc_client
    void set_rpc_client( rpc_client * );
    rpc_client *get_rpc_client() const;

    // request or subscription id
    void set_id( uint64_t );
    uint64_t get_id() const;

    // error code
    void set_err_code( int );
    int get_err_code() const;

    // time sent
    void set_sent_time( int64_t );
    int64_t get_sent_time() const;

    // time received reply
    void set_recv_time( int64_t );
    int64_t get_recv_time() const;

    // have we received a reply
    bool get_is_recv() const;

    // rpc response callback
    void set_sub( rpc_sub * );
    rpc_sub *get_sub() const;

    // is this message http or websocket bound
    virtual bool get_is_http() const;

    // request builder
    virtual void request( json_wtr& ) = 0;

    // response parsing and callback
    virtual void response( const jtree& ) = 0;

    // notification subscription update
    virtual bool notify( const jtree& );

  protected:

    template<class T> void on_response( T * );
    template<class T> bool on_error( const jtree&, T * );

  private:
    rpc_sub    *cb_;
    rpc_client *cp_;
    uint64_t    id_;
    int         ec_;
    int64_t     sent_ts_;
    int64_t     recv_ts_;
  };

  class rpc_subscription : public rpc_request
  {
  public:
    // subscriptions are only available on websockets
    virtual bool get_is_http() const;

    // add/remove this request to notification list
    void add_notify( const jtree& );
    void remove_notify();
  };

  /////////////////////////////////////////////////////////////////////////
  // wrappers for various solana rpc requests

  namespace rpc
  {
    // get account balance, program data and account meta-data
    class get_account_info : public rpc_request
    {
    public:
      // parameters
      void set_account( const pub_key& );

      // results
      uint64_t get_slot() const;
      uint64_t get_lamports() const;
      uint64_t get_rent_epoch() const;
      bool     get_is_executable() const;
      void     get_owner( const char *&, size_t& ) const;
      void     get_data( const char *&, size_t& ) const;

      get_account_info();
      void request( json_wtr& ) override;
      void response( const jtree& ) override;

    private:
      pub_key     acc_;
      uint64_t    slot_;
      uint64_t    lamports_;
      uint64_t    rent_epoch_;
      const char *dptr_;
      size_t      dlen_;
      const char *optr_;
      size_t      olen_;
      bool        is_exec_;
    };

    // recent block hash and fee schedule
    class get_recent_block_hash : public rpc_request
    {
    public:
      // results
      uint64_t get_slot() const;
      hash     get_block_hash() const;
      uint64_t get_lamports_per_signature() const;

      get_recent_block_hash();
      void request( json_wtr& ) override;
      void response( const jtree& ) override;

    private:
      uint64_t  slot_;
      hash      bhash_;
      uint64_t  fee_per_sig_;
    };

    // get validator node health
    class get_health : public rpc_request
    {
    public:
      void request( json_wtr& ) override;
      void response( const jtree& ) override;
    };

    // signature (transaction) subscription for tx acknowledgement
    class signature_subscribe : public rpc_subscription
    {
    public:
      // parameters
      void set_signature( const signature& );

      void request( json_wtr& ) override;
      void response( const jtree& ) override;
      bool notify( const jtree& ) override;

    private:
      signature sig_;
    };

    // transaction to transfer funds between accounts
    class transfer : public rpc_request
    {
    public:
      // parameters
      void set_block_hash( const hash& );
      void set_sender( const key_pair& );
      void set_receiver( const pub_key& );
      void set_lamports( uint64_t funds );

      // results
      signature get_signature() const;
      void enc_signature( std::string& );

      transfer();
      void request( json_wtr& ) override;
      void response( const jtree& ) override;

    private:
      hash      bhash_;
      key_pair  snd_;
      pub_key   rcv_;
      uint64_t  lamports_;
      signature sig_;
    };

    // create new account and assign ownership to some (program) key
    class create_account : public rpc_request
    {
    public:
      // parameters
      void set_block_hash( const hash& );
      void set_sender( const key_pair& );
      void set_account( const key_pair& );
      void set_owner( const pub_key& );
      void set_lamports( uint64_t funds );
      void set_space( uint64_t num_bytes );

      // results
      signature get_fund_signature() const;
      void enc_fund_signature( std::string& );
      signature get_acct_signature() const;
      void enc_acct_signature( std::string& );

      create_account();
      void request( json_wtr& ) override;
      void response( const jtree& ) override;

    private:
      hash      bhash_;
      key_pair  snd_;
      key_pair  account_;
      pub_key   owner_;
      uint64_t  lamports_;
      uint64_t  space_;
      signature fund_sig_;
      signature acct_sig_;
    };

  }

}
