program Subroutine_Call1
implicit none
! Syntax check
!Subroutine CALL
call randn(x(i))
call randn(x)
call random_number(U)
call rand_gamma0(a, .true., x)
call rand_gamma0(a, .true., x(1))
call rand_gamma0(a, .false., x(i))
call rand_gamma_vector_n(a, size(x), x)
call f(a=4, b=6, c=i)
call g(a(3:5,i:j), b(:))
call g(a(:5,i:j), b(1:))
call a%random_number(u)
call a%b%random_number(u)
call f(a=4, b=6, c=i)
call x%f%e()
end
