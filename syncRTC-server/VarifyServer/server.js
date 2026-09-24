const grpc = require("@grpc/grpc-js");
const message_proto = require("./proto");
const const_module = require("./const");
const { v4: uuidv4 } = require("uuid");
const emailModule = require("./email");
const redis_module = require("./redis");

async function GetVarifyCode(call, callback) {
  const email = call.request.email;
  console.log("email is", email);

  try {
    const redisKey = const_module.code_prefix + email;
    const existing = await redis_module.GetRedis(redisKey);
    console.log("query_res is", existing);

    let code = existing;
    if (code == null) {
      code = uuidv4();
      if (code.length > 4) {
        code = code.substring(0, 4);
      }

      const ok = await redis_module.SetRedisExpire(redisKey, code, 600);
      if (!ok) {
        callback(null, { email, error: const_module.Errors.RedisErr, code: "" });
        return;
      }
    }

    console.log("verify code is", code);

    // Keep mail content ASCII to avoid encoding issues.
    const text = `Your verify code is ${code} (valid for 10 minutes).`;
    const mailOptions = {
      from: "18370391278@163.com",
      to: email,
      subject: "Verify Code",
      text,
    };

    const send_res = await emailModule.SendMail(mailOptions);
    console.log("send res is", send_res);

    // Proto supports `code`; returning it helps when mail delivery is delayed/blocked.
    callback(null, { email, error: const_module.Errors.Success, code });
  } catch (error) {
    console.log("catch error is", error);
    callback(null, { email, error: const_module.Errors.Exception, code: "" });
  }
}

function main() {
  const server = new grpc.Server();
  server.addService(message_proto.VarifyService.service, { GetVarifyCode });
  server.bindAsync(
    "0.0.0.0:50051",
    grpc.ServerCredentials.createInsecure(),
    (err, port) => {
      if (err) {
        console.error("VerifyServer bind failed:", err);
        return;
      }
      server.start();
      console.log("grpc server started, port:", port);
    }
  );
}

main();
