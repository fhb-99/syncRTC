#include "rpc/VarifyGrpcClient.h"
#include "config/ConfigMgr.h"

VarifyGrpcClient::VarifyGrpcClient()
{
    auto& config = ConfigMgr::Init();
    std::string host = config["VarifyServer"]["Host"];
    std::string port = config["VarifyServer"]["Port"];
    m_rpc.reset(new RPConPool(5, host, port));
}

GetVarifyResponse VarifyGrpcClient::GetCode(std::string email)
{
    GetVarifyResponse reply;
    GetVarifyRequest request;
    request.set_email(email);

    auto stub = m_rpc->getConnection();
    if(!stub) {
        reply.set_error(ErrorCodes::RPCFailed);
        return reply;
    }

    ClientContext context;
    Status status = stub->GetVarifyCode(&context, request, &reply);
    m_rpc->returnConnection(std::move(stub));

    if(!status.ok()) {
        reply.set_error(ErrorCodes::RPCFailed);
    }
    return reply;
}