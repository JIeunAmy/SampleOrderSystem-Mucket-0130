// 진입점: Controller에 실행을 위임한다.
// 콘솔 출력/입력, 메뉴 분기, 계산 로직은 각각 view/controller/model/produce 계층의 책임이며
// main.cpp는 그 시작점 역할만 한다.

#include "controller/MenuController.h"

int main()
{
    MenuController controller;
    controller.Run();
    return 0;
}
